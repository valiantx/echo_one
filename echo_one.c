/**
 * An Echo Server & Client for testing libevent
 *
 * History: 2017-04-28 first version <shangshuai@gmail.com>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <event.h>
#include <signal.h>
#include <stdarg.h>

#define BUFFER_SIZE 1024
#define SERVER_PORT 5967
#define BACKLOG 5
#define KEEP_ALIVE_INERVAL 6
#ifndef NULL
#define NULL (void*)0
#endif
typedef enum { FALSE, TRUE} BOOL;
#define PROMPT "->"

//* Log */
#define LOG_FATAL 0
#define LOG_ERROR  1
#define LOG_WARNING  2
#define LOG_INFO  3
#define LOG_DEBUG 4

int cur_log_level = LOG_WARNING;
void set_log_level(int level) {
    cur_log_level = level;
}
BOOL check_log_level(int level) {
    return level <= cur_log_level;
}
#define LOG_BUF_SIZE 1000
#define PRINT(_fmt, ...) print_log(LOG_FATAL, _fmt"\n"PROMPT, ##__VA_ARGS__)
#define FATAL(_fmt, ...) print_log(LOG_FATAL, "[FATAL]"_fmt"\n"PROMPT, ##__VA_ARGS__)
#define ERROR(_fmt, ...) print_log(LOG_ERROR, "[ERROR]"_fmt"\n"PROMPT, ##__VA_ARGS__)
#define WARN(_fmt, ...) print_log(LOG_WARNING, "[WARNING]"_fmt"\n"PROMPT, ##__VA_ARGS__)
#define INFO(_fmt, ...) print_log(LOG_INFO, "[INFO]"_fmt"\n"PROMPT, ##__VA_ARGS__)
#define DEBUG(_fmt, ...) print_log(LOG_DEBUG, "[DEBUG]"_fmt"\n"PROMPT, ##__VA_ARGS__)

void print_log(int level, char* fmt, ...)
{
    if (!check_log_level(level)) {
        return;
    }

    va_list ap;
    char buf[LOG_BUF_SIZE];
    va_start(ap, fmt);
    int n = vsnprintf(buf, LOG_BUF_SIZE, fmt, ap);
    va_end(ap);

    write(STDOUT_FILENO, buf, n);
}

//* Events */
typedef struct EventOne {
	struct event_base* base;
	struct event read_ev;
	struct event write_ev;
	int data_len;
	char buffer[BUFFER_SIZE+8];    
	int read_fd;
	int write_fd;
	struct EventOne *pre;
	struct EventOne *next;
} EventOne;

EventOne* ev_list = NULL;
void on_read(int fd, short event, void* arg);

EventOne* event_one_new(struct event_base* base, int readfd, int writefd)
{
    EventOne* ev_ptr = (EventOne*)malloc(sizeof(*ev_ptr));
    memset(ev_ptr, 0, sizeof(*ev_ptr));
    if (ev_list) {
        ev_ptr->next = ev_list;
        ev_list->pre = ev_ptr;
    }
    ev_list = ev_ptr;

    ev_ptr->base = base;
    ev_ptr->read_fd = readfd;
    ev_ptr->write_fd = writefd;
    event_set(&ev_ptr->read_ev, readfd, EV_READ|EV_PERSIST, on_read, ev_ptr);
    event_base_set(ev_ptr->base, &ev_ptr->read_ev);
    event_add(&ev_ptr->read_ev, NULL);
    
    return ev_ptr;
}

void event_one_release(EventOne* ev_ptr)
{
    if (event_initialized(&ev_ptr->read_ev)) {
        event_del(&ev_ptr->read_ev);
    }
    if (event_initialized(&ev_ptr->write_ev)) {
        event_del(&ev_ptr->write_ev);
    }
    if (ev_ptr->pre) {
        ev_ptr->pre->next = ev_ptr->next;
    }
    if (ev_ptr->next) {
        ev_ptr->next->pre = ev_ptr->pre;
    }
    if (ev_ptr == ev_list) {
        ev_list = ev_list->next;
    }
    free(ev_ptr);
}

void event_one_release_all()
{
    EventOne* ev_ptr = ev_list;
    while (ev_ptr != NULL) {
        if (event_initialized(&ev_ptr->read_ev)) {
            event_del(&ev_ptr->read_ev);
        }
        if (event_initialized(&ev_ptr->write_ev)) {
            event_del(&ev_ptr->write_ev);
        }

        ev_ptr = ev_ptr->next;
        free(ev_ptr->pre);
    }
    ev_list = NULL;
}

void on_write(int fd, short event, void* arg)
{
    EventOne* ev_ptr = (EventOne*)arg;
    if (ev_ptr->data_len > 0) {
        if(STDOUT_FILENO == fd) {
            ev_ptr->data_len += sprintf(ev_ptr->buffer+ev_ptr->data_len, PROMPT);
        }
        write(fd, ev_ptr->buffer, ev_ptr->data_len);
        ev_ptr->data_len = 0;
    }
}

void on_read(int fd, short event, void* arg)
{
    struct event* write_ev;
    int size;
    EventOne* ev_ptr = (EventOne*)arg;
    ev_ptr->data_len = read(fd, ev_ptr->buffer, BUFFER_SIZE);
    if (ev_ptr->data_len <= 0) {
#if !defined(CLIENT)
        INFO("connection closed, fd %d", fd);
        event_one_release(ev_ptr);
        close(fd);
#else
        WARN("connection closed");
        event_base_loopexit(ev_ptr->base, NULL);
#endif
        return;
    }
    ev_ptr->buffer[ev_ptr->data_len] = 0;
    if (!strncmp("exit", ev_ptr->buffer, 4)) {
        INFO("will exit in 1 second");
        struct timeval delay = {1, 0};
        event_base_loopexit(ev_ptr->base, &delay);
        return;
    }
#if !defined(CLIENT)    
    DEBUG("receive from fd %d \'%s\'", fd, ev_ptr->buffer);
#endif
    event_set(&ev_ptr->write_ev, ev_ptr->write_fd, EV_WRITE, on_write, ev_ptr);
    event_base_set(ev_ptr->base, &ev_ptr->write_ev);
    event_add(&ev_ptr->write_ev, NULL);
}

//* server only*/
#if !defined(CLIENT)
void on_accept(int fd, short event, void* arg)
{
    struct sockaddr_in cli_addr;
    int newfd, sin_size;

    sin_size = sizeof(struct sockaddr_in);
    newfd = accept(fd, (struct sockaddr*)&cli_addr, &sin_size);
    INFO("new connection, fd %d", newfd);

    event_one_new((struct event_base*)arg, newfd, newfd);
}
#endif

void on_signal(int fd, short event, void* arg)
{
    switch(event) {
        case SIGINT:
        case SIGTSTP:
            INFO("will exit in 1 second");
            struct timeval delay = {1, 0};
            struct event_base* base = (struct event_base*)arg;
            event_base_loopexit(base, &delay);
            break;
        default:;
    }
}

//* Set TCP keep alive option to detect dead peers. The interval option 
// * is only used for Linux as we are using Linux-specific APIs to set 
// * the probe send time, interval, and count. */
int keep_alive(int fd, int interval)
{
    int val = 1;
    //enable keepalive
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1)  {
        return -1;
    }

    //* Default settings are more or less garbage, with the keepalive time
    // * set to 7200 by default on Linux. Modify settings to make the feature
    // * actually useful. */

    //* Send first probe after interval. */
    val = interval;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
        return -1;
    }

    //* Send next probes after the specified interval. Note that we set the
     //* delay as interval / 3, as we send three probes before detecting
     //* an error (see the next setsockopt call). */
    val = interval/3;
    if (val == 0) val = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
        return -1;
    }

    //* Consider the socket in error state after three we send three ACK
    // * probes without getting a reply. */
    val = 3;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
        return -1;
    }

    return 0;
} 

void usage() {
    PRINT(
        "-h <ip>    set server address\n"
        "-p <port>  set server port\n"
        "-v         verbose\n"
        "-V         more verbose\n"
        );
}

//* MAIN */
int main(int argc, char* argv[])
{
    //* get opts*/
#if !defined(CLIENT)
    PRINT("echo server");
    in_addr_t  server_ip = INADDR_ANY;
#else
    PRINT("echo client");
    in_addr_t  server_ip = inet_addr("127.0.0.1");
#endif
    unsigned short server_port = SERVER_PORT;

    int ch;
    opterr = 0;
    while ((ch = getopt(argc, argv, "hH:p:vV")) != -1) {
        switch(ch) {
            case 'h':
            case '?':
                usage();
                return 0;
            case 'H':
                server_ip = inet_addr(optarg);
                break;                
            case 'p':
                server_port = (unsigned short)atoi(optarg);
                break;
            case 'v':
                set_log_level(LOG_INFO);
                break;
            case 'V':
                set_log_level(LOG_DEBUG);
                break;
            default:;
        }
    }

    //* create socket */
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr_in;
    memset(&addr_in, 0, sizeof(addr_in));
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(server_port);
    addr_in.sin_addr.s_addr = server_ip;

#if !defined(CLIENT)
    int val = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
    bind(socket_fd, (struct sockaddr*)&addr_in, sizeof(struct sockaddr));
    if (listen(socket_fd, BACKLOG) < 0) {
        WARN("listen fail");
        return -1;
    }
    keep_alive(socket_fd, KEEP_ALIVE_INERVAL);
#else
    if (connect(socket_fd, (struct sockaddr *)&addr_in, sizeof(struct sockaddr)) < 0) {
        WARN("connect fail");
        return -1;
    }    
#endif

    //* events */
    struct event_base* base = event_base_new();

#if !defined(CLIENT)
    struct event listen_ev;
    event_set(&listen_ev, socket_fd, EV_READ|EV_PERSIST, on_accept, base);
    event_base_set(base, &listen_ev);
    event_add(&listen_ev, NULL);
    event_one_new(base, STDIN_FILENO, STDOUT_FILENO);
#else
    event_one_new(base, STDIN_FILENO, socket_fd);
    event_one_new(base, socket_fd, STDOUT_FILENO);
#endif

    //* signal CTRl_Z & CTRL_C */
    struct event* signal_int = evsignal_new(base, SIGINT, on_signal, base);
    event_add(signal_int, NULL);
    struct event* signal_stp = evsignal_new(base, SIGTSTP, on_signal, base);
    event_add(signal_stp, NULL);

    event_base_dispatch(base);

#if !defined(CLIENT)
    event_del(&listen_ev);
#endif
    event_one_release_all();
    event_free(signal_int);
    event_free(signal_stp);
    event_base_free(base);
    close(socket_fd);
    PRINT("bye");

    return 0;
}

