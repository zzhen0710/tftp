#include "client.hpp"
#include <arpa/inet.h>     // inet_addr()
#include <unistd.h>        // close()
#include <sys/time.h>      // struct timeval
#include <cstdlib>         // system()

using namespace std;

// 构造函数：创建 UDP 套接字，设置服务器结构体信息
TftpClient::TftpClient(const string& server_ip, const string& root_dir,
                       int server_port)
    : sock_fd_(-1), root_dir_(root_dir)   // 初始化列表，创建失败保持 -1
{
    // 创建 UDP 套接字
    sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd_ == -1) {
        ERR_LOG("sock error");
        return;
    }

    // 设接收超时：3 秒没收到包就返回 -1（errno == EAGAIN）
    struct timeval tv = {3, 0};
    if (setsockopt(sock_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        ERR_LOG("setsockopt error");
        return;
    }

    // 设置服务器结构体信息
    server_info_ = {
        AF_INET,
        htons(server_port),
        { inet_addr(server_ip.c_str()) }
    };
}

// 析构函数：以句柄关闭套接字
TftpClient::~TftpClient() 
{
    if (sock_fd_ > 0) close(sock_fd_);
}

void TftpClient::show_menu() 
{
    system("clear");

    cout << "\n";
    cout << "========================================\n";
    cout << "         TFTP 文件传输系统\n";
    cout << "========================================\n";
    cout << "   1. 下载文件\n";
    cout << "   2. 上传文件\n";
    cout << "   3. 退出\n";
    cout << "----------------------------------------\n";
    cout << "请选择 [1-3]: ";
}

// 判断客户端是否创建成功（套接字是否有效）
bool TftpClient::valid() const {
    return sock_fd_ != -1;
}

// 客户端主循环：显示菜单（和提示） → 读选择（并处理垃圾输入） → 分发到对应操作
void TftpClient::run() 
{
    int choice;          // 用户选择的菜单项
    string filename;     // 要上传/下载的文件名

    while (true) {
        show_menu();     // 清屏并显示菜单

        // 读菜单选择，失败（如输入字母）则清错误、提示重输
        if (!(cin >> choice)) {
            cin.clear();              // 清除 cin 的错误状态
            cin.ignore(10000, '\n');  // 丢弃缓冲区里的残留输入
            cout << "请输入数字！按任意键重试...";
            cin.get();                // 等一次按键
            continue;
        }
        cin.ignore(10000, '\n');      // 清掉 choice 后面残留的 '\n'，供下面 getline 用

        int ret = 0;                  // 操作结果：0 成功，-1 / -2 失败

        switch (choice) {
            case M_DOWNLOAD:          // 下载文件
                cout << "请输入文件名：";
                getline(cin, filename);   // 整行读入，允许文件名带空格
                ret = download(filename);
                break;

            case M_UPLOAD:            // 上传文件
                cout << "请输入文件名：";
                getline(cin, filename);
                ret = upload(filename);
                break;

            case M_EXIT:              // 退出程序
                cout << "欢迎下次使用！\n";
                return;               // 直接返回，结束 run()

            default:                  // 无效选择
                cout << "无效选择，按任意键重新输入...";
                cin.get();            // 等一次按键
                continue;             // 重新显示菜单
        }

        // 操作结束：成功/失败都提示，按任意键回主菜单
        if (ret == 0) {
            cout << "\n操作成功，按任意键返回主菜单...";
        } else if (ret == -2) {
            cout << "\n文件不存在，按任意键返回主菜单...";
        } else {
            cout << "\n操作失败，按任意键返回主菜单...";
        }
        cin.get();                // 等一次按键
    }
}