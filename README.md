**TCPSHM is a connection-oriented persistent message queue framework based on TCP or SHM IPC on Linux**

When using TCP to transfer data, sent out messages are not guaranteed to be received or handled by the receiver, and even worse, we often get unexpected disconnections due to network issues or program crash, so efforts are being made on recovery procedure to ensure both sides are synced. TCPSHM provides a reliable and efficient solution based on a sequence number and acknowledge mechanism, that every sent out msg is persisted in a send queue until sender got ack that it's been consumed by the receiver, so that disconnects/crashes are tolerated and the recovery process is purely automatic.

And as the name implies, shared memory is also supported when communicating on the same host, and it provides the same API and behavior as TCP(so whether TCP or SHM underlies the connection is transparent to the user), but it's more than 30 times faster than TCP on localhost(it takes around 100ns to transfer a small msg through SHM). The shared memory communication is based on [A real-time single producer single consumer msg queue](https://github.com/MengRao/SPSC_Queue).

Additionally, both sides of a connection have a specified name, and a pair of such names uniquely identifies a persistent connection. If one side disconnect and changes its name and reconnect with the same remote side, the connection will be brand new and will not recover from the old one. This can sometimes be useful, e.g: A daily trading server starts before market open and stops after market close every trading day, and every day when it starts it expects the connection with its clients to be new and any unhandled msgs from yesterday are silently discarded(obsolete order requests don't make any sense in a new trading day!), so the server can set its name to be something like "Server20180714".

This is a framework in that it provides a server side and client side C++ template class, which implement a typical tcp server and client and are also highly configurable and customizable. For server, the framework supports connection sharding: user predefines a set of connection groups, having one or more threads polling these groups, and once there's a new connection user can decide which group to assign to, so the framework gives the user full control over the mapping between serving threads and client connections.

## Technical Features
  * No C++ source files, only C++ headers, so no library to build and link
  * No external library dependencies
  * Non-blocking(except client side Connect())
  * No creating threads internally
  * No getting any kind of timestamp from system
  * No C++ execptions
  * No C++ virtual functions
  * No dynamic memory allocation(except a few std::string)
  * No writing to stdout or stderror
  * No use of mutexes
  * Yes, it's lightweight, clean and efficient
  
## Limitations
  * It won't persist data on disk, so it can't recover from power down
  * As it's non-blocking and busy waiting for the purpose of low latency, CPU usage would be high and a large number of live connections would downgrade the performance(say, more than 1000).
  * Transaction is not supported. So if you have multiple Push or Pop actions in a batch, be prepared that some succeed and some fail in case of program crash.
  * Client and server must have the same endianness, this limitation could be removed in the future.
  
## Documentation
  [Interface Doc](https://github.com/MengRao/tcpshm/blob/master/doc/interface.md)
  
## Example
  [Echo Client/Server](https://github.com/MengRao/tcpshm/tree/master/test) is a complete example.
  
## Guide to header files:

* **tcpshm_client.h**: The client side template class.

* **tcpshm_server.h**: The server side template class.

* **tcpshm_conn.h**: A general connection class that encapulates tcp or shm, use Alloc()/Push() and Front()/Pop() to send and recv msgs. You can get a connection object from client or server side interfaces.
