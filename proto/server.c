/*
 * server.c -- network protocol lab server
 */
#include "proto.h"
#include "util.h"
#include "code.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define DEBUG(x) x /**/

int
recv_message(int fd, void *buffer, size_t limit)
{
    struct sockaddr_storage address;
    socklen_t addr_len;
    int n; 

    struct sockaddr *addr = clr_sockaddr(&address, &addr_len); 
    n = recvfrom(fd, buffer, limit, 0, addr, &addr_len);
    if (n < 0) return n;  // recvfrom error

    if (filter_message(addr, buffer, n)) {
        return n;  // early (succesful) exit
    }

    DEBUG(dump_sockaddr(stdout, addr, addr_len));

    fputs("Message: \n", stdout);
    hexdump(stdout, buffer, n);

    return n;
}

//static BYTE proto_buf[256];  // message-transfer buffer
static BYTE proto_buf[2048];  // message-transfer buffer

int
server()
{
    int fd, rv, n;

    fd = create_socket();
    if (fd < 0) {
        perror("create_socket() failed");
        return -1;  // failure
    }

    rv = bind_socket(fd);
    if (rv < 0) {
        perror("bind_socket() failed");
        return -1;  // failure
    }

    while (true) {
        n = recv_message(fd, proto_buf, sizeof(proto_buf));
        if (n < 0) {
            perror("recv_message() failed");
            return -1;  // failure
        }
    }

    rv = close(fd);
    return rv;
}

int
main(int argc, char *argv[])
{
    int rv = parse_args(&argc, argv);
    if (rv != 0) return rv;

    fputs(argv[0], stdout);
    print_proto_opt(stdout);

    rv = server();

    return rv;
}
