#include <utility.h>
#include "client.h"

using namespace std;

int main(int argc, const char* argv[]) {
    if (argc < 2) {                          // 要求输入服务器 IP
        cerr << "用法: " << argv[0] << " <服务器IP> [端口] [本地目录]\n";
        return -1;
    }

    // 服务器 IP、工作目录、端口：命令行传
    string ip = argv[1];
    string root_dir = (argc > 2) ? argv[2] : ".";
    int port = (argc > 3) ? atoi(argv[3]) : TFTP_PORT;
    
    // 实例化客户端
    TftpClient client(ip, root_dir, port);
    if (!client.valid()) {                   // ← 用 valid() 判断，不用 try-catch
        fprintf(stderr, "客户端创建失败\n");
        return -1;
    }

    client.run();                            // 启动
    
    return 0;
}