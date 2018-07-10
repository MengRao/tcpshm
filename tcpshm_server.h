#pragma once
#include <string>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "tcpshm_conn.h"

template<class EventHandler, class Conf>
class TcpShmServer
{
public:
    using Connection = TcpShmConnection<Conf>;
    using LoginMsg = LoginMsgTpl<Conf>;
    using LoginRspMsg = LoginRspMsgTpl<Conf>;

    TcpShmServer(const std::string& server_name, const std::string& ptcp_dir, EventHandler* handler)
        : ptcp_dir_(ptcp_dir)
        , handler_(handler) {
        strncpy(server_name_, server_name.c_str(), sizeof(server_name_) - 1);
        server_name_[sizeof(server_name_) - 1] = 0;
        mkdir(ptcp_dir_.c_str(), 0755);
        for(auto& conn : conn_pool_) {
            conn.init(ptcp_dir.c_str(), server_name_);
        }
        int cnt = 0;
        for(auto& grp : shm_grps_) {
            for(auto& conn : grp.conns) {
                conn = conn_pool_ + cnt++;
            }
        }
        for(auto& grp : tcp_grps_) {
            for(auto& conn : grp.conns) {
                conn = conn_pool_ + cnt++;
            }
        }
    }

    ~TcpShmServer() {
        Stop();
    }

    bool Start(const char* listen_ipv4, uint16_t listen_port) {
        if(listenfd_ >= 0) {
            handler_->OnSystemError("already started", 0);
            return false;
        }

        if((listenfd_ = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            handler_->OnSystemError("socket", errno);
            return false;
        }

        fcntl(listenfd_, F_SETFL, O_NONBLOCK);
        int yes = 1;
        if(setsockopt(listenfd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
            handler_->OnSystemError("setsockopt SO_REUSEADDR", errno);
            return false;
        }

        struct sockaddr_in local_addr;
        local_addr.sin_family = AF_INET;
        inet_pton(AF_INET, listen_ipv4, &(local_addr.sin_addr));
        local_addr.sin_port = htons(listen_port);
        bzero(&(local_addr.sin_zero), 8);
        if(bind(listenfd_, (struct sockaddr*)&local_addr, sizeof(local_addr)) == -1) {
            handler_->OnSystemError("bind", errno);
            return false;
        }
        if(listen(listenfd_, 5) == -1) {
            handler_->OnSystemError("listen", errno);
            return false;
        }
        return true;
    }

    void PollCtl(int64_t now) {
        // every poll we accept only one connection
        if(avail_idx_ != Conf::MaxNewConnections) {
            NewConn& conn = new_conns_[avail_idx_];
            socklen_t addr_len = sizeof(conn.addr);
            conn.fd = accept(listenfd_, (struct sockaddr*)&(conn.addr), &addr_len);
            // we ignore errors from accept as most errno should be treated like EAGAIN
            if(conn.fd >= 0) {
                fcntl(conn.fd, F_SETFL, O_NONBLOCK);
                conn.time = now;
                avail_idx_ = Conf::MaxNewConnections;
            }
        }
        // visit all new connections, trying to read LoginMsg
        for(int i = 0; i < Conf::MaxNewConnections; i++) {
            NewConn& conn = new_conns_[i];
            if(conn.fd < 0) {
                avail_idx_ = i;
                continue;
            }
            int ret = ::recv(conn.fd, conn.recvbuf, sizeof(conn.recvbuf), 0);
            if(ret < 0 && errno == EAGAIN && now - conn.time <= Conf::NewConnectionTimeout) {
                continue;
            }
            if(ret == sizeof(conn.recvbuf) && conn.recvbuf[0].size == sizeof(MsgHeader) + sizeof(LoginMsg) &&
               conn.recvbuf[0].msg_type == LoginMsg::msg_type) {
                // looks like a valid login msg
                LoginMsg* login = (LoginMsg*)(conn.recvbuf + 1);
                if(login->use_shm) {
                    HandleLogin(now, conn, shm_grps_);
                }
                else {
                    HandleLogin(now, conn, tcp_grps_);
                }
            }

            if(conn.fd >= 0) {
                ::close(conn.fd);
                conn.fd = -1;
            }
            avail_idx_ = i;
        }

        for(auto& grp : shm_grps_) {
            for(int i = 0; i < grp.live_cnt;) {
                Connection& conn = *grp.conns[i];
                conn.TcpFront(now); // poll heartbeats, ignore return
                if(conn.IsClosed()) {
                    int sys_errno;
                    const char* reason = conn.GetCloseReason(sys_errno);
                    handler_->OnClientDisconnected(&conn, reason, sys_errno);
                    std::swap(grp.conns[i], grp.conns[--grp.live_cnt]);
                }
                else {
                    i++;
                }
            }
        }

        for(auto& grp : tcp_grps_) {
            for(int i = 0; i < grp.live_cnt;) {
                Connection& conn = *grp.conns[i];
                if(conn.IsClosed()) {
                    int sys_errno;
                    const char* reason = conn.GetCloseReason(sys_errno);
                    handler_->OnClientDisconnected(&conn, reason, sys_errno);
                    std::swap(grp.conns[i], grp.conns[--grp.live_cnt]);
                }
                else {
                    i++;
                }
            }
        }
    }

    void PollTcp(int64_t now, int grpid) {
        auto& grp = tcp_grps_[grpid];
        // force read grp.live_cnt from memory, it could have been changed by Ctl thread
        asm volatile("" : "=m"(grp.live_cnt) : :);
        for(int i = 0; i < grp.live_cnt; i++) {
            // it's possible that grp.conns is being swapped by Ctl thread
            // so some live conn could be missed, some closed one could be visited
            // even some conn could be visited twice, but they're all fine
            Connection& conn = *grp.conns[i];
            MsgHeader* head = conn.TcpFront(now);
            if(head && handler_->OnClientMsg(&conn, head)) {
                conn.TcpPop();
            }
        }
    }

    void PollShm(int grpid) {
        auto& grp = shm_grps_[grpid];
        asm volatile("" : "=m"(grp.live_cnt) : :);
        for(int i = 0; i < grp.live_cnt; i++) {
            Connection& conn = *grp.conns[i];
            MsgHeader* head = conn.ShmFront();
            if(head && handler_->OnClientMsg(&conn, head)) {
                conn.ShmPop();
            }
        }
    }

    void Stop() {
        if(listenfd_ < 0) {
            return;
        }
        ::close(listenfd_);
        listenfd_ = -1;
        for(int i = 0; i < Conf::MaxNewConnections; i++) {
            int& fd = new_conns_[i].fd;
            if(fd >= 0) {
                ::close(fd);
                fd = -1;
            }
        }
        avail_idx_ = 0;
        for(auto& grp : shm_grps_) {
            for(auto& conn : grp.conns) {
                conn->Release();
            }
            grp.live_cnt = 0;
        }
        for(auto& grp : tcp_grps_) {
            for(auto& conn : grp.conns) {
                conn->Release();
            }
            grp.live_cnt = 0;
        }
    }

private:
    struct NewConn
    {
        int64_t time;
        int fd = -1;
        struct sockaddr_in addr;
        MsgHeader recvbuf[1 + (sizeof(LoginMsg) + 7) / 8];
    };
    template<uint32_t N>
    struct alignas(64) ConnectionGroup
    {
        uint32_t live_cnt = 0;
        Connection* conns[N];
    };

    template<uint32_t N>
    void HandleLogin(int64_t now, NewConn& conn, ConnectionGroup<N>* grps) {
        MsgHeader sendbuf[1 + (sizeof(LoginRspMsg) + 7) / 8];
        sendbuf[0].size = sizeof(MsgHeader) + sizeof(LoginRspMsg);
        sendbuf[0].msg_type = LoginRspMsg::msg_type;
        LoginRspMsg* login_rsp = (LoginRspMsg*)(sendbuf + 1);
        strncpy(login_rsp->server_name, server_name_, sizeof(login_rsp->server_name));
        login_rsp->error_msg[0] = 0;

        LoginMsg* login = (LoginMsg*)(conn.recvbuf + 1);
        if(login->client_name[0] == 0) {
            strncpy(login_rsp->error_msg, "Invalid client name", sizeof(login_rsp->error_msg));
            ::send(conn.fd, sendbuf, sizeof(sendbuf), MSG_NOSIGNAL);
            return;
        }
        login->client_name[sizeof(login->client_name) - 1] = 0;
        int grpid = handler_->OnNewConnection(&conn.addr, login, login_rsp);
        if(grpid < 0) {
            if(login_rsp->error_msg[0] == 0) { // user didn't set error_msg? set a default one
                strncpy(login_rsp->error_msg, "Login Reject", sizeof(login_rsp->error_msg));
            }
            ::send(conn.fd, sendbuf, sizeof(sendbuf), MSG_NOSIGNAL);
            return;
        }
        auto& grp = grps[grpid];
        for(int i = 0; i < N; i++) {
            Connection& curconn = *grp.conns[i];
            char* remote_name = curconn.GetRemoteName();
            if(remote_name[0] == 0) { // found an unused one, use it then
                strncpy(remote_name, login->client_name, sizeof(login->client_name));
            }
            if(strncmp(remote_name, login->client_name, sizeof(login->client_name)) != 0) {
                // client name does not match
                continue;
            }
            // match
            if(i < grp.live_cnt) {
                strncpy(login_rsp->error_msg, "Already loggned on", sizeof(login_rsp->error_msg));
                ::send(conn.fd, sendbuf, sizeof(sendbuf), MSG_NOSIGNAL);
                return;
            }
            const char* error_msg;
            if(!curconn.Reset(login->use_shm, &sendbuf[0].ack_seq, &error_msg)) {
                // we can not mmap to ptcp or chm files with filenames related to local and remote name
                // in this case, curconn must not be open, i.e. i >= live_cnt
                handler_->OnSystemError(error_msg, errno);
                strncpy(login_rsp->error_msg, "System error", sizeof(login_rsp->error_msg));
                ::send(conn.fd, sendbuf, sizeof(sendbuf), MSG_NOSIGNAL);
                return;
            }

            if(::send(conn.fd, sendbuf, sizeof(sendbuf), MSG_NOSIGNAL) != sizeof(sendbuf)) {
                // curconn status is not changed(it could still be alive!)
                return;
            }
            int remote_ack_seq = conn.recvbuf[0].ack_seq;
            // if server_name has changed, reset the ack_seq
            if(strncmp(login->last_server_name, server_name_, sizeof(server_name_)) != 0) {
                remote_ack_seq = 0;
            }
            curconn.Open(conn.fd, remote_ack_seq, now);
            conn.fd = -1; // so it won't be closed by caller
            // switch to live
            std::swap(grp.conns[i], grp.conns[grp.live_cnt++]);
            handler_->OnClientLogon(&conn.addr, &curconn);
            return;
        }
        // no space for new remote name
        strncpy(login_rsp->error_msg, "Max client cnt exceeded", sizeof(login_rsp->error_msg));
        ::send(conn.fd, sendbuf, sizeof(sendbuf), MSG_NOSIGNAL);
    }

private:
    char server_name_[Conf::NameSize];
    std::string ptcp_dir_;
    EventHandler* handler_;
    int listenfd_ = -1;

    NewConn new_conns_[Conf::MaxNewConnections];
    int avail_idx_ = 0;

    Connection conn_pool_[Conf::MaxShmConnsPerGrp * Conf::MaxShmGrps + Conf::MaxTcpConnsPerGrp * Conf::MaxTcpGrps];
    ConnectionGroup<Conf::MaxShmConnsPerGrp> shm_grps_[Conf::MaxShmGrps];
    ConnectionGroup<Conf::MaxTcpConnsPerGrp> tcp_grps_[Conf::MaxTcpGrps];
};
