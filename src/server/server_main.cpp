#include "server.hpp"

using namespace std;

int main(int argc, const char* argv[]) {
    // 根目录、端口：命令行有就用，没有用默认
    string root_dir = (argc > 1) ? argv[1] : ".";
    int port = (argc > 2) ? atoi(argv[2]) : TFTP_PORT;
    
    // 实例化服务器
    TftpServer server(port, root_dir);
    if (!server.valid()) {
        fprintf(stderr, "服务器启动失败\n");
        return -1;
    }

    // 运行服务器
    server.run();
    
    return 0;
}