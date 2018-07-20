Echo Client/Server
==================

These are a pair of example programs for demonstrating and testing the TCPSHM framework. An echo client connects to the echo server and sends a sequence of increasing numbers through different types of msgs. The echo server just responds with the same msg and send back to the client. The client then checks if the received numbers are in a increasing sequence, and if all the numbers(up to some maximum) have been sent and received, it's done.

We can choose between tcp or shm modes, and use multiple clients from different locations concurrently connecting to the same server, and freely kill the clients or the server and restart them and see what happens.

```
root@DESKTOP-GQHGIKB:~/projects/tcpshm/test# ./echo_client client 127.0.0.1 0
Login Success
client started, send_num: 0 recv_num: 0
^C    # client is killed
root@DESKTOP-GQHGIKB:~/projects/tcpshm/test# ./echo_client client 127.0.0.1 0
Login Success
client started, send_num: 2023416 recv_num: 2023399
^C    # client is killed
root@DESKTOP-GQHGIKB:~/projects/tcpshm/test# ./echo_client client 127.0.0.1 0
Login Success
client started, send_num: 5403456 recv_num: 5403447
Client disconnected reason: Remote close syserrno: Success             # server is killed 
client stopped, send_num: 6451669 recv_num: 6451660 latency: 3604618681
root@DESKTOP-GQHGIKB:~/projects/tcpshm/test# ./echo_client client 127.0.0.1 0
Login Success
client started, send_num: 6451669 recv_num: 6451660
client stopped, send_num: 10000000 recv_num: 10000000 latency: 12019929234
```

## Building
Just run `./build.sh` to build, you can change the g++ compile options as you want.
