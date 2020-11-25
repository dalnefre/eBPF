/*
 * ebpf_fcgi.c -- eBPF "map" FastCGI server
 */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "fcgi.h"

#define DEBUG(x) x /**/

static char sock_pathname[] = "/run/ebpf_map.sock";

static char proto_buf[512];  // message-transfer buffer

static void
hexdump(FILE *f, void *data, size_t size)
{
    unsigned char *buffer = data;
    const int span = 16;
    size_t offset = 0;
    int i, j;

    while (offset < size) {
        fprintf(f, "%04zx:  ", offset);
        for (i = 0; i < span; ++i) {
            if (i == 8) {
                fputc(' ', f);  // gutter between 64-bit words
            }
            j = offset + i;
            if (j < size) {
                fprintf(f, "%02x ", buffer[j]);
            } else {
                fputs("   ", f);
            }
        }
        fputc(' ', f);
        fputc('|', f);
        for (i = 0; i < span; ++i) {
            j = offset + i;
            if (j < size) {
                unsigned char b = buffer[j];
                if ((0x20 <= b) && (b < 0x7F)) {
                    fprintf(f, "%c", (int)b);
                } else {
                    fputc('.', f);
                }
            } else {
                fputc(' ', f);
            }
        }
        fputc('|', f);
        fputc('\n', f);
        offset += span;
    }
    fflush(f);
}

int
server()
{
    int fd, rv, n;

    unlink(sock_pathname);  // remove stale UNIX domain socket, if any

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket() failed");
        return -1;  // failure
    }
    if (fd != FCGI_LISTENSOCK_FILENO) {
        perror("fd != FCGI_LISTENSOCK_FILENO");
        return -1;  // failure
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_pathname, sizeof(addr.sun_path) - 1);
    rv = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rv < 0) {
        perror("bind() failed");
        return -1;  // failure
    }

    rv = listen(FCGI_LISTENSOCK_FILENO, 4);
    if (rv < 0) {
        perror("listen() failed");
        return -1;  // failure
    }

    for (;;) {
        fd = accept(FCGI_LISTENSOCK_FILENO, NULL, NULL);
        if (fd < 0) {
            perror("accept() failed");
            continue;
        }

        n = read(fd, proto_buf, sizeof(proto_buf));
        if (n < 0) {
            perror("read() failed");
            return -1;  // failure
        }
        printf("read() got %d octets:\n", n);
        hexdump(stdout, proto_buf, sizeof(proto_buf));

        rv = close(fd);  // close client stream
    }

    rv = close(FCGI_LISTENSOCK_FILENO);  // close listener
    unlink(sock_pathname);  // remove UNIX domain socket
    return rv;
}

int
main(int argc, char *argv[])
{
    int rv;

    rv = close(FCGI_LISTENSOCK_FILENO);  // close stdin for listener to use
    if (rv < 0) {
        perror("close() failed");
        return -1;  // failure
    }

    rv = server();

    return rv;
}
