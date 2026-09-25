#ifndef _SERVER_HPP_
#define _SERVER_HPP_

#include "common.hpp"  
#include <string>       // std::string

// 服务器类：监听端口、处理 RRQ（下载）/ WRQ（上传）
class TftpServer {
private:
    int sock_fd_;                    // 服务器套接字文件描述符
    struct sockaddr_in server_info_; // 本地服务器地址信息结构体
    std::string root_dir_;           // 文件服务的根目录

    // 处理下载请求（RRQ）：把服务器文件发给客户端
    // filename / name_len：请求的文件名 + 长度（不依赖 '\0'）
    int do_download(const char* filename, int name_len,
                    struct sockaddr_in& cli_info);

    // 处理上传请求（WRQ）：接收客户端文件，存到服务器
    // filename / name_len：请求的文件名 + 长度（不依赖 '\0'）
    int do_upload(const char* filename, int name_len,
                  struct sockaddr_in& cli_info);

    // 发送 ERROR 包：错误码 + 错误信息（msg 指针 + msg_len 字节）
    int send_error(int err_code, const char* msg, int msg_len,
                   const struct sockaddr_in& cli_info);

public:
    // 一个服务器对象"独占"一个 socket，不该被拷贝
    TftpServer(const TftpServer&) = delete;
    TftpServer& operator=(const TftpServer&) = delete;

    // 构造：绑定端口，root_dir 为文件服务根目录
    TftpServer(int server_port = TFTP_PORT, const std::string& root_dir = ".");
    ~TftpServer();                   // 析构：关闭套接字

    bool valid() const;              // 判断服务器是否创建成功（sock_fd_ != -1）
    void run();                      // 主循环：提示 + 分发 RRQ/WRQ
};

#endif