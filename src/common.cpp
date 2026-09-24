#include <utility.h>
#include "common.h"

using namespace std;

// 收包（带超时重传 + 次数控制）
// 逻辑：循环 recvfrom；超时（EAGAIN）就重发 last_pkt、retry++；
//       收到包就返回；retry 超 max_retry 返回 -1
ssize_t recv_with_retry(int sock_fd, char* buf, int buf_size,
                        struct sockaddr_in* peer, socklen_t* peer_len,
                        const char* last_pkt, int last_len,
                        int max_retry)
{
    int retry = 0;

    while (true) {
        ssize_t ret = recvfrom(sock_fd, buf, buf_size, 0,
                               (struct sockaddr*)peer, peer_len);
        if (ret == -1) {
            // 超时（没收到包）：重发上一个包
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (++retry > max_retry) {
                    cout << "重传次数超限\n";
                    return -1;           // 超限，失败
                }
                cout << "超时：第 " << retry << " 次重发\n";
                sendto(sock_fd, last_pkt, last_len, 0,
                       (struct sockaddr*)peer, *peer_len);
                continue;                // 重发后继续等
            }
            // 真错误
            ERR_LOG("recvfrom error");
            return -1;
        }
        // 收到包，返回
        return ret;
    }
}