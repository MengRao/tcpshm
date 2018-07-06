#include <bits/stdc++.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "rdtsc.h"
#include "tcpshm_conn.h"

int main() {
    int listenfd, newfd;
    struct sockaddr_in local_addr, remote_addr;
    int addr_len;

    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    fcntl(listenfd, F_SETFL, O_NONBLOCK);
    int yes = 1;
    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    local_addr.sin_family = AF_INET;
    local_addr.port = htons(12345);
    local_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(local_addr.sin_zero), 8);
    if(bind(listenfd, (struct sockaddr*)&servaddr, sizeof(struct sockaddr)) == -1) {
        perror("bind");
        exit(1);
    }
    if(listen(listenfd, 5) == -1) {
        perror("listen");
        exit(1);
    }

    std::vector<TCPSHMConnection> conns;
    conns.reserve(10);
    char buf[1024];
    static const int kTmpSocks = 10;
    static const int64_t kTimeOut = 12345678901LL;
    static const int kLoginMsgSize = sizeof(MsgHeader) + sizeof(LoginMsg);
    std::pair<int64_t, int> new_socks[kTmpSocks];
    for(int i = 0; i < kTmpSocks; i++) new_socks[i].second = -1;

    while(true) {
        int64_t now = rdtsc();
        newfd = accept(listenfd, (struct sockaddr*)&remote_addr, &addr_len);
        if(newfd == -1) {
            if(errno != EAGAIN) {
                perror("accept");
            }
        }
        else {
            printf("new connection from %s\n", inet_ntoa(remote_addr.sin_addr));
            fcntl(newfd, F_SETFL, O_NONBLOCK);
            int i = 0;
            for(; i < kTmpSocks; i++) {
                if(new_socks[i].second < 0) break;
                if(now - new_sock[i].first > kTimeOut) {
                    close(new_socks[i].second);
                    break;
                }
            }
            if(i == kTmpSocks) {
                fprintf(stderr, "too many tmp connections");
                close(newfd);
            }
            else {
                new_sock[i] = {now, newfd};
            }
        }
        // handle new socks
        for(int i = 0; i < kTmpSocks; i++) {
            if(new_sock[i].second >= 0) {
                bool do_close = false;
                int ret = ::recv(new_socks[i].second, buf, kLoginMsgSize, 0);
                if(ret == kLoginMsgSize) {
                }
                else if(ret < 0 && errno == EAGAIN) {
                    if(now - new_sock[i].first > kTimeOut) {
                        do_close = true;
                    }
                }
                else {
                    do_close = true;
                }


                if(do_close) {
                    close(new_socks[i].second);
                    new_socks[i].second = -1;
                }
            }
        }
    }

    return 0;
}
