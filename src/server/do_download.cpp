#include <utility.h>
#include "server.h"

using namespace std;

// ============================================================
//  处理下载请求（RRQ）—— 服务器视角（含超时重传）
// ------------------------------------------------------------
//  服务器角色：接收 RRQ，把本地文件"发"给客户端（发 DATA，收 ACK）
//  和客户端 upload 相反：这里"服务器发数据、客户端回 ACK"
//
//  【正常流程】
//  1. 收到 RRQ，打开 root_dir/filename
//     - 打不开 → send_error(ERR_FILE_NOT_FOUND)，结束
//  2. 发 DATA(1)（块号从 1 开始），等客户端的 ACK(1)
//  3. 收到 ACK(N)：
//     - 匹配 → 读下一块、发 DATA(N+1)，等 ACK(N+1)
//  4. 最后一块（数据 < 512）发出后，等它的 ACK(N)，收到后结束
//
//  【异常处理】
//  - 超时没收到 ACK → 重发上一个包（上一个 DATA）
//  - 收到块号不匹配的 ACK（重复/过期）→ 忽略，继续等
//
//  【RFC 1123】
//  - 作为发送方，收到不匹配的 ACK 时，绝不立即重发 DATA，
//    只忽略、继续等；重传只由"超时"触发（避免重传风暴）。
//
//  【关键规则】
//  - 块号：DATA 从 1 开始，num 初值 = 1，每收到匹配 ACK 后 +1
//  - 发 DATA 前更新 last_pkt（供超时重发）
//  - 超时重传 + 次数控制：统一交给 recv_with_retry 处理
// ============================================================
int TftpServer::do_download(const char* filename, int name_len,
                            struct sockaddr_in& cli_info)
{
    LOG_PKT("【服务器】RRQ 文件 = %s", string(filename, name_len).c_str());

    // 拼接要下载的完整路径
    string full_path = root_dir_ + "/" + string(filename, name_len);
    // 以只读形式打开文件
    int read_fd = open(full_path.c_str(), O_RDONLY);
    if (read_fd == -1) {    // 分情况发 ERROR 包
        if (errno == ENOENT) {   // 文件不存在
            const char* msg = "File Not Found";
            send_error(ERR_FILE_NOT_FOUND, msg, strlen(msg), cli_info);
        } else {    // 权限不够、其他错误等
            const char* msg = "Access violation";
            send_error(ERR_ACCESS, msg, strlen(msg), cli_info);
        }
        return -1;   // 结束本次处理
    }

    char buf[BUF_SIZE];         // 发送 DATA 缓冲区
    ssize_t recv_size = 0;      // 记录接收包尺寸 
    unsigned short num = 1;     // 编号

    // "上一个发出的包"：每次 sendto 前更新，供超时重发
    char last_pkt[BUF_SIZE];
    int  last_len = 0;

    // 循环封装发送 DATA，并接收 ACK
    while (true) {
        memset(buf, 0, sizeof(buf));    // 清空缓冲区

        // 封装 DATA
        buf[1] = OP_DATA;
        *(unsigned short*)(buf + 2) = htons(num);
        // 从服务器本地文件读一块数据到"数据段"（buf + 4 之后）
        ssize_t read_size = read(read_fd, buf + 4, DATA_SIZE);
        if (read_size == -1) {
            ERR_LOG("read error");
            close(read_fd);
            return -1;
        }

        // 存包，供超时重发
        memcpy(last_pkt, buf, read_size + 4);
        last_len = read_size + 4;

        // 发出 DATA 包（read = 0 时，发送空包）
        socklen_t info_size = sizeof(cli_info);
        if (sendto(sock_fd_, last_pkt, last_len, 0,
                (const sockaddr*)&cli_info, info_size) == -1) {
            ERR_LOG("sendto error");
            close(read_fd);
            return -1;
        }
        LOG_PKT("→ DATA 块号 = %d 长度 = %d", num, (int)last_len);

        // 等待获取 ACK
        recv_size = recv_with_retry(sock_fd_, buf, BUF_SIZE,
                                    &cli_info, &info_size,
                                    last_pkt, last_len);
        if (recv_size == -1) {
            ERR_LOG("recvfrom error");
            close(read_fd);
            return -1;
        }
        LOG_PKT("← ACK  块号 = %d", ntohs(*(unsigned short*)(buf + 2)));
        
        // 解析获得的包
        if (buf[1] == OP_ACK) {
            // 块号不匹配 → 忽略，回循环顶部重新等（RFC 1123：不因收到不对的包重传）
            if (num != ntohs(*(unsigned short*)(buf + 2))) {
                continue;
            }

            // 数据不足一块（< 512，含 0 字节空包）→ 最后一块
            // 注意：它的 ACK 就是"外层刚收到的这个"，不用再等
            if (read_size < DATA_SIZE) {
                // 等客户端对最后一块的 ACK：必须是 ACK，且块号 == num
                cout << "==============文件下载完毕==============\n";
                close(read_fd);
                return 0;
            }
            // 用于下轮编号 +1
            ++num;

        } else if (buf[1] == OP_ERROR) {   // 收到 ERROR 包
            cout << "______error: " << (buf + 4) << "________" << endl;
            close(read_fd);
            return -1;
        }
    }

    return 0;
}