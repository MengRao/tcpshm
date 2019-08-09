**TCPSHM is a connection-oriented persistent message queue framework based on TCP or SHM IPC for Linux**

When using TCP to transfer data, sent out messages are not guaranteed to be received or handled by the receiver, and even worse, we often get unexpected disconnections due to network issues or program crash, so efforts have been made on recovery procedure to ensure both sides are synced. TCPSHM provides a reliable and efficient solution based on a sequence number and acknowledge mechanism, that every sent out msg is persisted in a send queue until sender got ack that it's been consumed by the receiver, so that disconnects/crashes are tolerated and the recovery process is purely automatic.

And as the name implies, shared memory is also supported when communicating on the same host, and it provides the same API and behavior as TCP(so whether TCP or SHM underlies the connection is transparent to the user), but it's more than 20 times faster than TCP on localhost(However TCP is still 3 times faster than ZeroMQ IPC, see [Performance](https://github.com/MengRao/tcpshm#performance)). The shared memory communication is based on [A real-time single producer single consumer msg queue](https://github.com/MengRao/SPSC_Queue).

The user message format is just a general purpose binary string, it's user's responsibility to encode/decode it. E.g. user can simply use C/C++ struct for simplicity/efficiency, or google protocol buffer for extensibility.

Additionally, both sides of a connection have a specified name, and a pair of such names uniquely identifies a persistent connection. If one side disconnect and changes its name and reconnect with the same remote side, the connection will be brand new and will not recover from the old one. This can sometimes be useful, e.g: A daily trading server starts before market open and stops after market close every trading day, and every day when it starts it expects the connection with its clients to be new and any unhandled msgs from yesterday are silently discarded(obsolete order requests don't make any sense in a new trading day!), so the server can set its name to be something like "Server20180714".

This is a framework in that it provides a server side and client side C++ template class, which implement a typical tcp server and client and are also highly configurable and customizable. For server, the framework supports connection sharding: user predefines a set of connection groups, having one or more threads polling these groups, and once there's a new connection user can decide which group to assign to, so the framework gives the user full control over the mapping between serving threads and client connections.

## Technical Features
  * No source files, only header files, so no library to build and link
  * No external library dependencies
  * Non-blocking(except client Connect())
  * No creating threads internally
  * No getting any kind of timestamp from system
  * No C++ execptions
  * No writing to stdout/stderror or log file
  * No use of mutex or atomic operations
  * Yes, it's lightweight, clean and efficient
  
## Limitations
  * It won't persist data on disk, so it can't recover from power down
  * As it's non-blocking and busy polling for the purpose of low latency, CPU usage would be high and a large number of live connections would downgrade the performance(say, more than 1000).
  * Currently user can only write to a connection in its polling(reading) thread. If needing to write msg from other threads, user has to push it to some queue which is then consumed by the polling thread.
  * Transaction is not supported. So if you have multiple Push or Pop actions in a batch, be prepared that some succeed and some fail in case of program crash.
  * Currently the message length must fit in a uint16_t(including the 8 bytes header). It's possible to make it configurable in the future(e.g. take 2 bytes from ack_seq because sequence number wraparound is already properly handled).
  
## Documentation
  [Interface Doc](https://github.com/MengRao/tcpshm/blob/master/doc/interface.md)
  
## Example
  [Echo Client/Server](https://github.com/MengRao/tcpshm/tree/master/test) is a complete example.
  
## Performance
The echo client/server example is also used as performance test, where the client sends a message to the server on the same host, waiting for the response from server before sending the next one... Each message has a variable size which is randomly chosen in (16, 36, 68, 200) bytes. The test is done on an Ubuntu 14.04 host with Intel(R) Xeon(R) CPU E5-2687W 0 @ 3.10GHz, and cpupin is enabled in both client and server to get more stable latency(prevent process from being preempted). We use average RTT(Round Trip Time) to measure latency(avg rtt = total time elapsed / number of messages processed):

RTT in **TCP** mode: **8.81837 us**

RTT in **SHM** mode: **0.338221 us**

For comparison, below are performance of **ZeroMQ(Zero-Copy REQ/REP)** using exactly the same benchmark method:

RTT in **ZMQ TCP** mode: **42.268 us**

RTT in **ZMQ IPC(Unix Domain Socket)** mode: **30.6534 us**
  
## Guide to header files:

* **tcpshm_client.h**: The client side template class.

* **tcpshm_server.h**: The server side template class.

* **tcpshm_conn.h**: A general connection class that encapulates tcp or shm, use Alloc()/Push() and Front()/Pop() to send and recv msgs. You can get a connection reference from client or server side interfaces, and send msgs to it even if it's currently disconnected from remote peer.
