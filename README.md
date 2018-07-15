**TCPSHM is a recoverable message communication framework based on TCP or shared memory IPC for Linux**

When using TCP to transfer data, sent out messages are not guaranteed to be received and handled by the receiver, and even worse, we often get unexpected disconnections due to either network issues or program crash, so efforts are being made on recovery procedure to ensure both sides are synced. TCPSHM provides a reliable and efficient solution based on sequence number and acknowledge mechanism that every sent out msg is persisted in a send queue until we got ack that it's been consumed by the remote side, so that disconnects/crashes are tolerated and the recovery process is purely automatic.

And as the name implies, shared memory is also supported when communicating on the same host, and provides the same interface and behavior as TCP(so users can use the same code to handle both modes), but it's more than 20 times faster than TCP on localhost!

Both sides of a connection have a name, and a pair of such names uniquely identifies a connection. If one side disconnect and changes its name and reconnect with the same remote side, the connection will be brand new and will not recover from the old connection. This can sometimes be useful, e.g: A daily trading server starts before market open and stops after market close every trading day, and every day it starts it expects the connection with its clients to be new and any unhandled msgs from yesterday are silently discarded(obsolete order requests don't make any sense in a new trading day!), so the server can set its name to be something like "Server20180714".

This is a framework in that it provides a server side and client side C++ template class, which implement a typical tcp server and client and are also highly configurable and customizable. For server, the framework supports connection sharding: user predefines a set of connection groups, and have one or more threads polling these groups, and once there's a new login request user decides which group it's to be assigned to. So the framework gives the user full control over the mapping between serving threads and client connections.

## Technical Features
  * No C++ source files, only C++ headers, so no library to build and link
  * No external library dependencies
  * Non-blocking(only client side Connect() blocks)
  * No creating threads internally
  * No getting any kind of timestamp from system
  * No C++ execptions
  * No C++ virtual functions
  * No dynamic memory allocation(only used a few std::string)
  * No writing to stdout or stderror
  * No use of mutexes
  * Yes, it's lightweight, clean and efficient
  
## Limitations
  * As it's non-blocking and busy waiting for the purpose of low latency, CPU usage would be high and large number of live connections would downgrade the performance(say, more than 1000).
  * Transaction is not supported. So if you have multiple Push or Pop actions in a batch, be prepared that some succeed and some fail in case of program crash.
  
## Documentation
  [Interface Doc](https://github.com/MengRao/tcpshm/blob/master/doc/interface.md)
  
## Example
  [Echo Client/Server](https://github.com/MengRao/tcpshm/tree/master/test) is a complete example.
  
## Guide to header files:

* **tcpshm_client.h**: The client side template class.

* **tcpshm_server.h**: The server side template class.

* **tcpshm_conn.h**: A general connection class that encapulates tcp or shm, use Alloc()/Push() and Front()/Pop() to send and recv msgs. You can get a connection object from client or server side interfaces.
