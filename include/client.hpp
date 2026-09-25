#ifndef _CLIENT_HPP_
#define _CLIENT_HPP_

#include "common.hpp"
#include <string>       // std::string

// 客户端类：连接服务器、上传/下载文件
class TftpClient {
private:
    int sock_fd_;                    // 客户端套接字 fd
    struct sockaddr_in server_info_; // 目标服务器地址
    std::string root_dir_;                // 客户端本地读写目录

    int upload(std::string& filename);    // 上传文件
    int download(std::string& filename);  // 下载文件
    void show_menu();                 // 展示菜单

public:
    // 一个客户端对象"独占"一个 socket，不该被拷贝
    TftpClient(const TftpClient&) = delete;             // 禁拷贝构造
    TftpClient& operator=(const TftpClient&) = delete;  // 禁拷贝赋值

    TftpClient(const std::string& server_ip, const std::string& root_dir = ".",
               int server_port = TFTP_PORT); // 构造：指定服务器 IP 和端口
    ~TftpClient();                        //析构函数

    bool valid() const;                   // 判断客户端是否创建成功（sock_fd_ != -1）
    void run();                           // 客户端主循环：菜单 + 分支选择处理
};

#endif