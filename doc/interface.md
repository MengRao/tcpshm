tcpshm
======

## MsgHeader and TcpShmConnection
Every msg has a `MsgHeader` automatically appended, regardless of control msg or app msg, it's a 8 byte structure in host endian:

```c++
struct MsgHeader
{
    // size of this msg, including header itself
    // auto set by lib, can be read by user
    uint16_t size;
    // msg type of app msg is set by user and must not be 0
    uint16_t msg_type;
    // internally used for ptcp, must not be modified by user
    uint32_t ack_seq;
};
```
The framework will apply endian conversion on MsgHeader automatically(check ToLittleEndian below) if sending over tcp channel.

TcpShmConnection is a general connection class that we can use to send or recv msgs.
**Note that reading/writing msgs on one connection must happen in the same thread: its polling thread(see [Limitations](https://github.com/MengRao/tcpshm#limitations)).**
For sending, user calls Alloc() to allocate space to save a msg:

```c++
    // allocate a msg of specified size in send queue
    // the returned address is guaranteed to be 8 byte aligned
    // return nullptr if no enough space
    MsgHeader* Alloc(uint16_t size);
```

In the returned `MsgHeader` pointer, user need to set msg_type field and the msg content(it's user's responsibility to take care of the endian for msg content) after the header, then call Push() to submit and send out the msg.
If user have multiple msgs to send in a row, it's better to use PushMore() for first several msgs and Push() for the last one:
```c++
    // submit the last msg from Alloc() and send out
    void Push();

    // for shm, same as Push
    // for tcp, don't send out immediately as we have more to push
    void PushMore();
```

For receiving, user calls Front() to get the first app msg in receive queue, but normally Front() should be automatically called by framework in polling functions:
```c++
    // get the next msg from recv queue, return nullptr if queue is empty
    // the returned address is guaranteed to be 8 byte aligned
    // if caller dont call Pop() later, it will get the same msg again
    // user dont need to call Front() directly as polling functions will do it
    MsgHeader* Front();
```
If returned `MsgHeader` is not nullptr, user can identify basic msg info from its msg_type and size and handle msg content after `MsgHeader`.
If user finishes handling the msg, it should call Pop() to consume it, otherwise user will get the same msg again from the next Front():
```c++
    // consume the msg we got from Front() or polling function
    void Pop();
```

In a typical scenario that on handling a msg, user wants to send back a response msg immediately, he should call Pop() and Push() in a row instead of the reverse, in that:
1) for tcp, Push() will send to the network which would be slow, so if we do the reverse there's a chance that when program crashes the Pushed msg is persisted in sending queue but Pop() is not called, so on recovery it'll handle the same msg again and push a duplicate response. If we do Pop() and Push() there's still a chance that Pop() succeeds but Push() doesn't(miss sending a response), but that's only a theoretical chance, you can test the EchoServer example.  
2) for tcp, if we call Pop() and Push(), the updated ack seq(due to Pop()) will be piggybacked by the response msg(due to Push()), which means the remote side will get the update more quickly.

User can close the connection and the remote side will get the disconnect notification.
```c++
    // Close this connection
    void Close();
```

In application, user is not allowed to create TcpShmConnection but can get a reference to it from client or server framework, and this reference is guaranteed to be valid until server/client is stopped, this allows user to send msgs even when it's disconnected, and remote side will get them once connection is re-established. 

Connection related configuration are as below:
```c++
struct Conf
{
    // the size of client/server name in chars, including the ending null
    static const uint32_t NameSize = 16;
    
    // shm queue size, must be a power of 2
    static const uint32_t ShmQueueSize = 2048;

    // set to the endian of majority of the hosts, e.g. true for x86
    static const bool ToLittleEndian = true; 

    // tcp send queue size, must be a multiple of 8
    static const uint32_t TcpQueueSize = 2000; 

    // tcp recv buff init size(recv buffer is allocated when tcp connection is established), must be a multiple of 8
    static const uint32_t TcpRecvBufInitSize = 2000;

    // tcp recv buff max size(recv buffer can expand when needed), must be a multiple of 8
    static const uint32_t TcpRecvBufMaxSize = 8000;

    // if enable TCP_NODELAY
    static const bool TcpNoDelay = true;

    // tcp connection timeout, measured in user provided timestamp
    static const int64_t ConnectionTimeout = 10;

    // delay of heartbeat msg after the last tcp msg send time, measured in user provided timestamp
    static const int64_t HeartBeatInverval = 3;

    // user defined data in LoginMsg, e.g. username, password..., take care of the endian
    using LoginUserData = char;

    // user defined data in LoginRspMsg, take care of the endian
    using LoginRspUserData = char;

    // user defined data in TcpShmConnection class
    using ConnectionUserData = char;
};
```


## Client Side
tcpshm_client.h defines template Class `TcpShmClient`, user need to defines a new Class that derives from `TcpShmClient` and provides a configuration template class, and also a client name and ptcp folder name for TcpShmClient's constructor. The client name is used combined with server name to uniquely identify a connection, and the ptcp folder is used by the framework to persist some internal files including the tcp queue file.

```c++
#include "tcpshm/tcpshm_client.h"

struct Conf
{
    // Connection related Conf
    ...
};

class MyClient;
using TSClient = tcpshm::TcpShmClient<MyClient, Conf>;

class MyClient : public TSClient 
{
public:
    MyClient(const std::string& ptcp_dir, const std::string& name)
        : TSClient(ptcp_dir, name)
...
```

Then user can call Connect() to login to the server:

```c++
    // connect and login to server, may block for a short time
    // return true if success
    bool Connect(bool use_shm, // if using shm to transfer application msg
                 const char* server_ipv4, // server ip
                 uint16_t server_port, // server port
                 const typename Conf::LoginUserData& login_user_data //user defined login data to be copied into LoginMsg
                );
```

If Login successful, user can get the Connection reference to send msgs:

```c++
    // get the connection reference which can be kept by user as long as TcpShmClient is not destructed
    Connection& GetConnection();
```

For receiving msgs and keeping the connection alive, user needs to frequenctly poll the client. For tcp mode user calls PollTcp(); For shm mode user calls both PollTcp() and PollShm(), from the same or different thread, using seperate thread has the advantage that app msg latency will be lower.

```c++
    // we need to PollTcp even if using shm
    // now is a user provided timestamp, used to measure ConnectionTimeout and HeartBeatInverval
    void PollTcp(int64_t now);

    // only for using shm
    void PollShm();
```

To stop the client, just call Stop()
```c++
    // stop the connection and close files
    void Stop();
```

Also, user needs to define a collection of callback functions for framework to invoke:
```c++
    // called within Connect()
    // reporting errors on connecting to the server
    void OnSystemError(const char* error_msg, int sys_errno);

    // called within Connect()
    // Login rejected by server
    void OnLoginReject(const LoginRspMsg* login_rsp);

    // called within Connect()
    // confirmation for login success
    // return timestamp of now
    int64_t OnLoginSuccess(const LoginRspMsg* login_rsp);

    // called within Connect()
    // server and client ptcp sequence number don't match, we need to fix it manually
    void OnSeqNumberMismatch(uint32_t local_ack_seq,
                             uint32_t local_seq_start,
                             uint32_t local_seq_end,
                             uint32_t remote_ack_seq,
                             uint32_t remote_seq_start,
                             uint32_t remote_seq_end);

    // called by APP thread
    // handle a new app msg from server
    void OnServerMsg(MsgHeader* header);

    // called by tcp thread
    // connection is closed
    void OnDisconnected(const char* reason, int sys_errno);
```

## Server Side
tcpshm_server.h defines template Class `TcpShmServer`, same as `TcpShmClient`, user need to defines a new Class that derives from `TcpShmServer` and provides a configuration template class, and also a server name and ptcp folder name for TcpShmServer's constructor:
```c++
#include "tcpshm/tcpshm_server.h"

struct Conf
{
    // Connection related Conf:
    ...

    // Server related Conf:

    // max number of unlogined tcp connection
    static const uint32_t MaxNewConnections = 5;

    // max number of shm connection per group
    static const uint32_t MaxShmConnsPerGrp = 4;

    // number of shm connection groups
    static const uint32_t MaxShmGrps = 1;

    // max number of tcp connections per group
    static const uint32_t MaxTcpConnsPerGrp = 4;
    
    // number of tcp connection groups
    static const uint32_t MaxTcpGrps = 1;

    // unlogined tcp connection timeout, measured in user provided timestamp
    static const int64_t NewConnectionTimeout = 3;
};

class MyServer;
using TSServer = TcpShmServer<MyServer, Conf>;

class MyServer : public TSServer
{
public:
    MyServer(const std::string& ptcp_dir, const std::string& name)
        : TSServer(ptcp_dir, name) 
...
```
User starts and stops the server by Start() and Stop():
```c++
    // start the server
    // return true if success
    bool Start(const char* listen_ipv4, uint16_t listen_port);
    
    void Stop();
```

One important feature of the server is it allows user to customize their threading model. 
The max number of threads it supports is MaxShmGrps + MaxTcpGrps + 1(for control thread), in this case, each group is served by a seperate thread.
To the opposite extreme, user can have only one thread serving all.
This logic is controlled by how user calls polling functions in his thread(s).
Server has 3 polling functions, and they can be called from the same or different threads: 
```c++
    // poll control for handling new connections and keep shm connections alive
    void PollCtl(int64_t now);

    // poll tcp for serving tcp connections
    void PollTcp(int64_t now, int grpid);

    // poll shm for serving shm connections
    void PollShm(int grpid);
```

Also, user needs to define a collection of callback functions for framework to invoke:
```c++
    // called with Start()
    // reporting errors on Starting the server
    void OnSystemError(const char* errno_msg, int sys_errno);

    // called by CTL thread
    // if accept the connection, set user_data in login_rsp and return grpid with respect to tcp or shm
    // else set error_msg in login_rsp if possible, and return -1
    // Note that even if we accept it here, there could be other errors on handling the login,
    // so we have to wait OnClientLogon for confirmation
    int OnNewConnection(const struct sockaddr_in& addr, const LoginMsg* login, LoginRspMsg* login_rsp);

    // called by CTL thread
    // ptcp or shm files can't be open or are corrupt
    void OnClientFileError(Connection& conn, const char* reason, int sys_errno);

    // called by CTL thread
    // server and client ptcp sequence number don't match, we need to fix it manually
    void OnSeqNumberMismatch(Connection& conn,
                             uint32_t local_ack_seq,
                             uint32_t local_seq_start,
                             uint32_t local_seq_end,
                             uint32_t remote_ack_seq,
                             uint32_t remote_seq_start,
                             uint32_t remote_seq_end);

    // called by CTL thread
    // confirmation for client logon
    void OnClientLogon(const struct sockaddr_in& addr, Connection& conn);

    // called by CTL thread
    // client is disconnected
    void OnClientDisconnected(Connection& conn, const char* reason, int sys_errno); 

    // called by APP thread
    void OnClientMsg(Connection& conn, MsgHeader* recv_header);
```
