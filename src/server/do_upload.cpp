#include <utility.h>
#include "server.h"

using namespace std;

// ============================================================
//  处理上传请求（WRQ）—— 服务器视角（含超时重传）
// ------------------------------------------------------------
//  服务器角色：接收 WRQ，把客户端发来的文件"存"到本地
//  （先回 ACK(0)，再收 DATA、回 ACK）
//  和客户端 download 相反：这里"客户端发数据、服务器回 ACK"
//
//  【正常流程】
//  1. 收到 WRQ，在 root_dir 下创建/打开文件（准备接收）
//     - 创建失败 → send_error(ERR_ACCESS)，结束
//  2. 回 ACK(0)（告诉客户端"可以开始发"）
//  3. 收到 DATA(N)：
//     - 匹配 → 写入文件，回 ACK(N)
//  4. 收到的 DATA 长度 < 516（数据段 < 512），说明是最后一块：
//     回 ACK(N) 后，传输结束
//
//  【异常处理】
//  - 超时没收到 DATA → 重发上一个包（ACK(0) 或 上一个 ACK）
//  - 收到块号不匹配的 DATA（重复/过期）→ 忽略
//    （可选：重发上一个 ACK，帮对方前进）
//
//  【RFC 1123】
//  - 服务器是"接收方"（收 DATA、回 ACK）：
//    收到重复的 DATA 时，可立即重发上一个 ACK（RFC 1350 建议，优化）。
//
//  【关键规则】
//  - 块号：先回 ACK(0)，收到 DATA(N) 后回 ACK(N)，num 初值 = 0
//  - 回 ACK 前更新 last_pkt（供超时重发）
//  - 超时重传 + 次数控制：统一交给 recv_with_retry 处理
// ============================================================
int TftpServer::do_upload(const char* filename, int name_len,
                          struct sockaddr_in& cli_info)
{
    LOG_PKT("【服务器】WRQ 文件 = %s", string(filename, name_len).c_str());

    //找到要上传的文件路径
    string full_path = root_dir_ + "/" + string(filename, name_len); //C++中字符串可以是+拼接
    //以只写的形式打开文件，获取句柄
    int write_fd = open(full_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (write_fd < 0) {
        const char* msg = "Access violation"; // 发给对端识别，间接打印，并非直接打印，不需要'\n'
        send_error(ERR_ACCESS, msg, strlen(msg), cli_info);   // ← strlen，不是 sizeof
        return -1;
    }

    char buf[BUF_SIZE];         // 接收 DATA 缓冲区
    ssize_t recv_size = 0;      // 记录接收包尺寸 
    unsigned short num = 0;     // 编号，从 0 开始

    // "上一个发出的包"：每次 sendto 前更新，供超时重发
    char last_pkt[BUF_SIZE];
    int  last_len = 0;

    while (true) {
        memset(buf, 0, sizeof(buf));    // 清空缓冲区

        // 封装 ACK
        buf[1] = OP_ACK;
        *(unsigned short*)(buf + 2) = htons(num);

        // 发前存包，供超时重发
        memcpy(last_pkt, buf, 4);
        last_len = 4;

        // 发出 ACK
        socklen_t info_size = sizeof(cli_info);
        if (sendto(sock_fd_, last_pkt, last_len, 0,
                (const sockaddr*)&cli_info, info_size) == -1) {
            ERR_LOG("sendto error");
            close(write_fd);
            return -1;
        }
        LOG_PKT("→ ACK  块号 = %d", num);

        // 接收读取服务器发来的消息
        recv_size = recv_with_retry(sock_fd_, buf, BUF_SIZE,
                                    &cli_info, &info_size,
                                    last_pkt, last_len);
        if (recv_size == -1) {
            ERR_LOG("recvfrom error");
            close(write_fd);
            return -1;           
        }
        LOG_PKT("← DATA 块号=%d 长度=%d",
                ntohs(*(unsigned short*)(buf + 2)), (int)recv_size);

        // 解析获得的包
        if (buf[1] == OP_DATA) {  // 判断是否为 DATA 类型
            unsigned short expect = num + 1;   // 期望收到的块号
            // 块号不匹配 → 立即重发 ACK，帮助接收方确认，回到循环顶等待发送端恢复
            if (expect != ntohs(*(unsigned short*)(buf + 2))) {
                // 只在"已经回过 ACK"后，才重发 ACK；
                if (sendto(sock_fd_, last_pkt, last_len, 0,
                            (struct sockaddr*)&cli_info, info_size) == -1) {
                    ERR_LOG("sendto error");
                    close(write_fd);
                    return -1;
                }
                continue;
            }
            num = expect;

            // 将数据包中数据写入文件
            if (write(write_fd, buf + 4, recv_size - 4) == -1) {
                cerr << "write_fd = "<< write_fd << ", recv_size = "<< recv_size << endl;
                ERR_LOG("write error");
                close(write_fd);
                return -1;
            }

            // 最后判断所接受数据包大小是否不足满缓冲区，并补发最后一块（收到的数据 < 516）
            if (recv_size < BUF_SIZE) {
                // 回最后一块的 ACK
                buf[1] = OP_ACK;
                *(unsigned short*)(buf + 2) = htons(num);   // num = 刚收到的块号
                if (sendto(sock_fd_, buf, 4, 0,
                        (struct sockaddr*)&cli_info, info_size) == -1) {
                    ERR_LOG("sendto error");
                    close(write_fd);
                    return -1;
                }
                cout << "==============文件上传完毕==============\n";
                close(write_fd);
                break;
            }

        } else if (buf[1] == OP_ERROR) {
            cout << "______error: " << (buf + 4) << "________" << endl;
            close(write_fd); // 关闭本地写文件
            return -1;
        }
    }

    return 0;
}