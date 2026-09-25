#ifndef _COMMON_H_
#define _COMMON_H_

#include <sys/types.h>      // ssize_t
#include <netinet/in.h>     // sockaddr_in
#include <sys/socket.h>     // socklen_t
#include <iostream>         // std::cerr
#include <cerrno>           // errno
#include <cstdio>           // printf

#define TFTP_PORT 8888    // 双方约定端口号
#define BUF_SIZE  516     // 储存一条TFTP报文的缓冲区大小
#define DATA_SIZE 512     // DATA 数据段大小

#define TIMEOUT_SEC   3    // 接收超时秒数
#define MAX_RETRY     5    // 最大重传次数

#define ERR_FILE_NOT_FOUND 1   // 请求的文件不存在
#define ERR_ACCESS         2   // 创建/写文件失败
#define ERR_ILLEGAL_OP     4   // opcode 非法

// 错误日志宏函数，用do-while(0)进行单行包裹，防止if()内文本展开报错
#define ERR_LOG(msg) do { \
    perror(msg); \
    std::cerr << __LINE__ << "  " << __func__ << "  " << __FILE__ << endl; \
} while(0)

//  调试日志宏（打印通信交互过程）
#define TFTP_DEBUG 1     // 1 = 打印，0 = 关闭（发布时关）

#if TFTP_DEBUG
    #define LOG_PKT(fmt, ...) do { \
        printf("[PKT] " fmt "\n", ##__VA_ARGS__); \
    } while(0)
#else
    #define LOG_PKT(fmt, ...) do {} while(0)
#endif

// 菜单枚举选项映射
enum MenuChoice {
    M_DOWNLOAD = 1,   // 下载文件
    M_UPLOAD   = 2,   // 上传文件
    M_EXIT     = 3    // 退出
};

// TFTP 操作码枚举（对应报文前 2 字节的 opcode 字段）
enum TftpOpcode {
    OP_RRQ   = 1,     // 读请求（下载）
    OP_WRQ   = 2,     // 写请求（上传）
    OP_DATA  = 3,     // 数据包
    OP_ACK   = 4,     // 确认包
    OP_ERROR = 5      // 错误包
};

// 收包（带超时重传 + 次数控制）
ssize_t recv_with_retry(int sock_fd, char* buf, int buf_size,
                        struct sockaddr_in* peer, socklen_t* peer_len,
                        const char* last_pkt, int last_len,
                        int max_retry = MAX_RETRY);

#endif