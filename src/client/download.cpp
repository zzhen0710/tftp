#include <utility.h>
#include "client.h"

using namespace std;

// ============================================================
//  下载（RRQ）交互流程（含超时重传）
// ------------------------------------------------------------
//  【正常流程】
//  1. 客户端发 RRQ，等服务器的 DATA(1)
//  2. 收到 DATA(N)：
//     - 匹配 → 写入文件，回 ACK(N)
//     - 回 ACK 后，等下一个 DATA(N+1)
//  3. 收到的 DATA 长度 < 516（数据段 < 512），说明是最后一块：
//     回 ACK(N) 后，传输结束
//
//  【异常处理】
//  - 超时没收到 DATA → 重发上一个包（RRQ 或 ACK）
//  - 收到块号不匹配的 DATA（重复/过期）→ 立即重发上一个 ACK
//
//  【RFC 1123】
//  - 区分两件事：
//      · 发送方（如 upload）收到不匹配的 ACK：绝不立即重发 DATA，
//        只忽略并继续等，重传只由超时触发（避免重传风暴）。
//        ← RFC 1123 明确规定
//      · 接收方（如 download）收到重复的 DATA：立即重发上一个 ACK，
//        帮对方前进——这不是"重传数据"，安全。
//        ← RFC 1350 给出的建议（优化，非强制；不发也行，靠对方超时重传，不死锁）
//
//  【关键规则】
//  - 块号：RRQ 直接收 DATA(1)，num 初值 = 1，每确认一块后 +1
//  - 超时重传 + 次数控制：统一交给 recv_with_retry 处理
// ============================================================
int TftpClient::download(string& filename) 
{
    // 封装 RRQ （下载请求包）
    char buf[BUF_SIZE] = ""; // 封装缓冲区
    // 拼 RRQ 包 —— opcode(0x0001)读 + 文件名 + \0 + 模式"octet"二进制逐字节传输 + \0
    ssize_t snd_size = sprintf(buf, "%c%c%s%c%s%c",
        0, OP_RRQ, filename.c_str(), 0, "octet", 0);

    // "上一个发出的包"：每次 sendto 前更新，供超时重发
    char last_pkt[BUF_SIZE];
    int  last_len = 0;

    // 发 RRQ：先存 last_pkt，再发（保证"存的和发的一致"）
    memcpy(last_pkt, buf, snd_size);
    last_len = snd_size;

    // 向服务器发送请求（该 RRQ 包）
    socklen_t info_size = sizeof(server_info_); // 服务器信息大小
    if (sendto(sock_fd_, last_pkt, last_len, 0,
                (struct sockaddr*)&server_info_, info_size) == -1) {
        ERR_LOG("sendto error");
        return -1;
    }
    LOG_PKT("→ RRQ  请求 %s", filename.c_str());

    // 循环接收服务器发来的包
    ssize_t recv_size = 0;   // 记录接收包尺寸
    unsigned short num = 1; // 记录数据块编号，2字节无符号数
    
    bool flg = false;   // 标识文件是否被打开
    bool has_acked = false;   // 是否已经回过至少一个 ACK（决定"不匹配时重发什么"）
    int write_fd = -1;  // 用于将 DATA 本地写的fd

    while (true) {
        memset(buf, 0, sizeof(buf)); // 清空缓冲区

        // 接收读取服务器发来的消息
        recv_size = recv_with_retry(sock_fd_, buf, BUF_SIZE,
                                    &server_info_, &info_size,
                                    last_pkt, last_len);
        if (recv_size == -1) {
            ERR_LOG("recvfrom error");
            close(write_fd);
            return -1;           
        }
        LOG_PKT("← DATA 块号 = %d 长度 = %d",
                ntohs(*(unsigned short*)(buf + 2)), (int)recv_size);

        // 解析获得的包
        if (buf[1] == OP_DATA) {  // 判断是否为 DATA 类型
            // 块号不匹配 → 立即重发 ACK，帮助接收方确认，回到循环顶等待发送端恢复
            if (num != ntohs(*(unsigned short*)(buf + 2))) {
                // 只在"已经回过 ACK"后，才重发 ACK；
                // 否则（还在等 DATA(1)，last_pkt 是 RRQ）只忽略，避免重发 RRQ
                if (has_acked) {
                    if (sendto(sock_fd_, last_pkt, last_len, 0,
                                (struct sockaddr*)&server_info_, info_size) == -1) {
                        ERR_LOG("sendto error");
                        close(write_fd);
                        return -1;
                    }
                }
                continue;
            }

            if (flg == 0) { // 判断暂时无句柄用来在本地写文件
                // 注意：filename 是相对路径，open 会相对"当前工作目录"（即运行程序的地方）查找，不是源文件所在目录
                std::string path = root_dir_ + "/" + filename;  // 拼"本地路径"
                write_fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (write_fd == -1) {
                    ERR_LOG("open error");
                    return -1;           
                }
                flg = true; // 更改标志，说明已有该本地写文件句柄
            }

            // 将数据包中数据写入文件
            if (write(write_fd, buf + 4, recv_size - 4) == -1) {
                cerr << "write_fd = "<< write_fd << ", recv_size = "<< recv_size << endl;
                ERR_LOG("write error");
                close(write_fd);
                return -1;
            }

            // 把收到的 DATA 头改造成 ACK：opcode 改成 ACK（块号不变）
            buf[1] = OP_ACK;
            // 发 ACK 前，存一份（供超时重发）并标记 has_acked
            memcpy(last_pkt, buf, 4);
            last_len = 4;
            has_acked = true;
            // 打包 ACK 报文，发回服务器，就是 DATA 前 4 位，编号与 DATA 一致不必改 ACK，编号由上传端改
            // 在检查大小之前发送，确保 DATA < BUF_SIZE 也发送最后一个 ACK
            if (sendto(sock_fd_, last_pkt, last_len, 0,
                        (struct sockaddr*)&server_info_, info_size) < 0) {
                ERR_LOG("sendto error");
                close(write_fd);
                return -1;
            }
            LOG_PKT("→ ACK  块号 = %d", num);

            // 最后判断所接受数据包大小是否不足满缓冲区
            if (recv_size < BUF_SIZE) {
                cout << "==============文件下载完毕==============" << endl;
                close(write_fd);
                break;  // 正常退出
            }

            ++num; // 本地对比编号 +1

        } else if (buf[1] == OP_ERROR) { // 说明有错误信息
            cout << "______error: " << (buf + 4) << "________" << endl;
            if (flg == true) close(write_fd); // 如果有，关闭本地写文件
            return -1;
        }
    }

    return 0; // 成功返回 0
}