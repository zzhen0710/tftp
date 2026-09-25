#include "client.hpp"        
#include <fcntl.h>         // open(), O_RDONLY
#include <unistd.h>        // read(), close()
#include <sys/stat.h>      // mode_t（open 的权限参数，可选）
#include <cstring>         // memset(), memcpy()

using namespace std;

// ============================================================
//  上传（WRQ）交互流程（含超时重传）
// ------------------------------------------------------------
//  【正常流程】
//  1. 客户端发 WRQ，等服务器的 ACK(0)
//  2. 收到 ACK(N)：
//     - 匹配 → 块号 +1，读文件一块，打包成 DATA(N+1)，发出去
//     - 发 DATA 后，等下一个 ACK(N+1)
//  3. 最后一块（数据 < 512，含 0 字节空包）发出后，
//     仍要等服务器对它的 ACK(N)，收到后结束
//
//  【异常处理】
//  - 超时没收到 ACK → 重发上一个包（WRQ 或 DATA）
//  - 收到块号不匹配的 ACK（重复/过期）→ 忽略，继续等
//
//  【RFC 1123】
//  - 作为发送方，收到不匹配的 ACK 时，绝不立即重发 DATA，只忽略并继续等；
//    重传只由"超时"触发，避免"巫师学徒综合征"（重传风暴）。
//
//  【关键规则】
//  - 块号：WRQ 的第一个期望回应是 ACK(0)，num 初值 = 0，
//          收到匹配的 ACK 后 +1，DATA 块号 = num
//  - 超时重传 + 次数控制：统一交给 recv_with_retry 处理
// ============================================================
int TftpClient::upload(string& filename)
{
    // 交互前，先检查文件是否存在
    // filename 是文件名，open 只在"运行程序的工作目录"里查找
    // 拼"本地路径"：root_dir_ + "/" + filename
    string path = root_dir_ + "/" + filename;
    int read_fd = open(path.c_str(), O_RDONLY);
    if (read_fd < 0) {
        if (errno == ENOENT) return -2;   // 文件不存在 → 返回 -2
        ERR_LOG("open error");
        return -1;
    }

    char buf[BUF_SIZE] = "";
    // 拼 WRQ 包 —— opcode(0x0002)写 + 文件名 + \0 + 模式"octet" + \0
    ssize_t snd_size = sprintf(buf, "%c%c%s%c%s%c",
        0, OP_WRQ, filename.c_str(), 0, "octet", 0);

    // "上一个发出的包"：每次 sendto 前更新，供超时重发
    char last_pkt[BUF_SIZE];
    int  last_len = 0;

    // 发 WRQ：先存 last_pkt，再发（保证"存的和发的一致"）
    memcpy(last_pkt, buf, snd_size);
    last_len = snd_size;

    socklen_t info_size = sizeof(server_info_);
    // 发出 WRQ 包
    if (sendto(sock_fd_, last_pkt, last_len, 0,
                (struct sockaddr*)&server_info_, info_size) == -1) {
        ERR_LOG("sendto error");
        close(read_fd);
        return -1;
    }
    LOG_PKT("→ WRQ  请求 %s", filename.c_str());

    unsigned short num = 0;   // 期望块号（对 WRQ 初始期望块号为 0）

    while (true) {
        memset(buf, 0, sizeof(buf));   // 清空缓冲区

        // 收包（带超时重传 + 次数控制）：收到返回长度，超限返回 -1
        ssize_t ret = recv_with_retry(sock_fd_, buf, BUF_SIZE,
                                      &server_info_, &info_size,
                                      last_pkt, last_len);
        if (ret == -1) {
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

            // 把收到的 ACK 缓冲改造成 DATA 包：对 WRQ 收到 ACK 后主动修改块号 = num + 1
            buf[1] = OP_DATA;
            *(unsigned short*)(buf + 2) = htons(++num);

            // 从本地文件读一块数据到"数据段"（buf + 4 之后）
            ssize_t read_size = read(read_fd, buf + 4, DATA_SIZE);
            if (read_size == -1) {
                ERR_LOG("read error");
                close(read_fd);
                return -1;
            }

            // 发 DATA 前，用"实际长度"更新 last_pkt（供超时重发）
            memcpy(last_pkt, buf, read_size + 4);
            last_len = read_size + 4;

            // 发出 DATA 包（read = 0 时，发送空包）
            if (sendto(sock_fd_, last_pkt, last_len, 0,
                       (struct sockaddr*)&server_info_, info_size) < 0) {
                ERR_LOG("sendto error");
                close(read_fd);
                return -1;
            }
            LOG_PKT("→ DATA 块号 = %d 长度 = %d", num, (int)(read_size + 4));

            // 数据不足一块（< 512，含 0 字节空包）→ 最后一块
            if (read_size < DATA_SIZE) {
                // 等服务器对最后一块的 ACK：必须是 ACK，且块号 == num
                while (true) {
                    ssize_t r = recv_with_retry(sock_fd_, buf, BUF_SIZE,
                                                &server_info_, &info_size,
                                                last_pkt, last_len);
                    if (r == -1) {
                        close(read_fd);
                        return -1;
                    }
                    // 是 ACK 且块号匹配 → 传输完成
                    if (buf[1] == OP_ACK &&
                        num == ntohs(*(unsigned short*)(buf + 2))) {
                        break;
                    }
                    // 不匹配 → 忽略，继续等（RFC 1123：不重传）
                }
                cout << "==============文件上传完毕==============\n";
                close(read_fd);
                return 0;
            }
            
        } else if (buf[1] == OP_ERROR) {   // 收到 ERROR 包
            cout << "______error: " << (buf + 4) << "________" << endl;
            close(read_fd);
            return -1;
        }
    }

    return 0;
}