/*
 * Copyright (C) 2015, Ivan Krivonos (int0dster@gmail.com) 
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * This is a test for issue 557. The server opens a socket, accepts connection
 * and sets up the socket buffer size to some small value (16 bytes).
 * The client opens a socket, sets predefined size (16k bytes) and then issues
 * few writes to server. It fills in the send buffer and at end of the process
 * all the free buffer space in the send buffer shall be consumed. If it is true
 * - the test passes. If it not true - it fails.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <string.h>
#include <sys/ioctl.h>

#include <thread>


// server() opens a listening TCP server on port 1234, accepts one connection,
// and then waits until client fills both server receive buffer
// and client send buffer with the data 
static constexpr short LISTEN_TCP_PORT = 1234;
static int server(void)
{
    int listenfd;
    struct sockaddr_in sa;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("open listening socket");
        return 1;
    };
    int i=1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));
    bzero(&sa, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(LISTEN_TCP_PORT);
    if (bind(listenfd, (struct sockaddr *)&sa, sizeof(sa))<0) {
        perror("bind listen_socket");
        abort();
    }
#define CONF_LISTEN_BACKLOG 10
    if (listen(listenfd, CONF_LISTEN_BACKLOG)<0) {
        perror("listen");
        abort();
    }

    int fd = accept(listenfd, NULL, NULL);
    if (fd < 0) {
        perror("accept failed");
        close(listenfd);
        return 1;
    }
    /* fd is in LISTEN state. SIOCOUTQ shall return EINVAL */
    int unused;
    int err = ioctl(listenfd, SIOCOUTQ, &unused);
    if ((err == 0) || (err < 0 && errno != EINVAL)) {
       perror("ioctl failed");
       close(listenfd);
       return 1;
    }
    /* 
     * do nothing for 5 seconds to allow client to send all data
     */
    printf("server: sleep for 5 seconds..\n");
    sleep(5);
    return 0;
}

static int client(void)
{
    int sock;
    struct sockaddr_in sa;
    char buf[8192];
    int socket_size = 16 * 1024;
    int unsent;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("open socket");
        return 1;
    };
    bzero(&sa, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons(LISTEN_TCP_PORT);
    if (connect(sock, (struct sockaddr *)&sa, sizeof(sa))<0) {
        perror("connect socket");
        abort();
    }
    if (ioctl(sock, SIOCOUTQ, &unsent) < 0) {
        perror("ioctl failed");
        abort();
    }
    socklen_t opt_size = sizeof(socket_size);
    if (setsockopt(sock, SOL_SOCKET,
         SO_SNDBUF, &socket_size, sizeof(socket_size)) < 0) {
           perror("setsockopt failed");
           abort();
    }
    if (getsockopt(sock, SOL_SOCKET,
         SO_SNDBUF, &socket_size, &opt_size) < 0) {
           perror("getsockopt failed");
           abort();
    }
    printf("client: at first stage unsent %d total socket size %d\n", 
       unsent, socket_size);

    /* set non-blocking mode so we will get EWOULDBLOCK when buffers get filled */
    (void) fcntl(sock, F_SETFL, O_NONBLOCK);

    for (;;) {

       int wr = write(sock, buf, sizeof(buf));

       if (wr == 0 || (wr < 0 &&
              (errno == EWOULDBLOCK || errno == EAGAIN)))
           break;

       if (ioctl(sock, SIOCOUTQ, &unsent) < 0) {
           perror("ioctl");
           abort();
        }
        printf("unsent %d\n", unsent);
    }
    if (unsent != socket_size) {
       printf("FAILURE, unsent (%d) != socket size (%d)\n",
            unsent, socket_size);
       return 1;
    }
    close(sock);
    return 0;
}

static int test(void)
{
    int srv_result;
    std::thread t([&srv_result] { srv_result = server(); });
    sleep(1);
    int clnt_result = client();
    t.join();
    return srv_result == 0 && clnt_result == 0;
}

static int tests = 0, fails = 0;
static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}


int main(int argc, char **argv)
{
    report(test(), "SIOCOUTQ test");
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails;
}
