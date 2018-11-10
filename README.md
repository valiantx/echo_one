# echo_one
an echo server and client for testing libevent

**BUILD**

*example*:

- gcc echo_one.c -L /usr/local/lib -static -levent -lrt -DSERVER -o echo_server
- gcc echo_one.c -L /usr/local/lib -static -levent -lrt -DCLIENT -o echo_client

