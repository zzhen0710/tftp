#include <utility.h>
#include "server.h"

using namespace std;

// 构造：绑定端口，root_dir 为文件服务根目录
TftpServer::TftpServer(int server_port, const std::string& root_dir)
    : sock_fd_(-1), root_dir_(root_dir)
{
    // 创建 UDP 套接字
    sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock_fd_ < 0) {
        ERR_LOG("socker error");
        return;
    }

    // 填充地址信息结构体
    server_info_ = {
        AF_INET,
        htons(server_port),
        { INADDR_ANY }
    };

    // 设置端口号快速重用：避免服务器重启时 "Address already in use"
    int opt = 1;
    if (setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ERR_LOG("setsockopt error");
        return;
    }

    // 设接收超时：3 秒没收到包，recvfrom 返回 -1（errno == EAGAIN）
    struct timeval tv = {3, 0};
    if (setsockopt(sock_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        ERR_LOG("setsockopt error");
        return;
    }

    // 绑定工作
    if(bind(sock_fd_, (struct sockaddr*)&server_info_, sizeof(server_info_)) <0) {
        ERR_LOG("bind error");
        return;
    }
}

// 析构：关闭套接字
TftpServer::~TftpServer()
{
    if (sock_fd_ > 0) close(sock_fd_);
}

// 判断服务器是否创建成功（套接字是否有效）
bool TftpServer::valid() const {
    return sock_fd_ != -1;
}

// 发送 ERROR 包：opcode(5) + 错误码(2字节) + 错误信息（msg_len 字节）+ '\0'
// msg / msg_len：错误信息内容 + 长度（由调用方传入）
int TftpServer::send_error(int err_code, const char* msg, int msg_len,
                           const struct sockaddr_in& cli_info)
{
    char buf[BUF_SIZE] = "";

    buf[1] = OP_ERROR;                              // opcode = 5
    *(unsigned short*)(buf + 2) = htons(err_code);  // 错误码（网络序）
    memcpy(buf + 4, msg, msg_len);                  // 按长度拷
    buf[4 + msg_len] = '\0';                        // 结尾 '\0'

    int len = 4 + msg_len + 1;                      // 包总长

    // ERROR 包是单向通知，发一次即完，不等回应、不重传
    if (sendto(sock_fd_, buf, len, 0,
               (struct sockaddr*)&cli_info, sizeof(cli_info)) < 0) {
        return -1;
    }
    return 0;
}

// 服务器主循环：阻塞收包 → 解析 opcode → 分发 RRQ/WRQ
void TftpServer::run()
{
    char buf[BUF_SIZE];
    struct sockaddr_in cli_info;
    socklen_t cli_len = sizeof(cli_info);

    cout << "服务器已启动，等待请求...\n";

    while (true) {
        memset(buf, 0, sizeof(buf));
        cli_len = sizeof(cli_info);   // 每次收包前重置

        // 这里不用 recv_with_retry：主循环等的是"任意客户端的新请求"，
        // 没有"上一个要重发的包"，超时只需 continue 继续等
        ssize_t ret = recvfrom(sock_fd_, buf, BUF_SIZE, 0,
                               (struct sockaddr*)&cli_info, &cli_len);
        if (ret == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;  // 超时、无数据时，重试
            ERR_LOG("recvfrom error");
            continue;
        }

        // opcode（前 2 字节，大端）
        unsigned short opcode = ntohs(*(unsigned short*)buf);

        // 文件名：opcode 之后开始，到 '\0' 为止
        const char* filename = buf + 2;
        int name_len = strlen(filename);   // 文件名的长度（不含 '\0'）

        cout << "收到请求：opcode =" << opcode
             << "，文件 = " << filename
             << "，来自 "  << inet_ntoa(cli_info.sin_addr) << endl;

        switch (opcode) {
            case OP_RRQ: {   // 读请求 → 客户端要下载
                int r = do_download(filename, name_len, cli_info);
                if (r == -1) {
                    // 该函数内部已发 ERROR 包 / 打日志，这里只记录
                    cerr << "do_download 失败，fd 等已由内部处理\n";
                }
                break;
            }

            case OP_WRQ: {   // 写请求 → 客户端要上传
                int r = do_upload(filename, name_len, cli_info);
                if (r == -1) {
                    cerr << "do_upload 失败，fd 等已由内部处理\n";
                }
                break;
            }

            default: {       // 非法包
                const char* msg = "非法操作码";
                int r = send_error(ERR_ILLEGAL_OP, msg, strlen(msg), cli_info);
                if (r == -1) {
                    ERR_LOG("send_error error");   // 发 ERROR 包失败
                }
                break;
            }
        }
    }
}