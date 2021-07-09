/*
 * link_srv.c -- link protocol lab server
 */
#include "proto.h"
#include "util.h"
#include "code.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define DEBUG(x) x /**/

static octet_t proto_buf[1024];  // message-transfer buffer

int
server(void *buffer, size_t limit)
{
    int fd, rv, n;
    struct sockaddr_storage address;
    socklen_t addr_len;

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
        memset(buffer, 0, limit);  // clear buffer
        struct sockaddr *addr = clr_sockaddr(&address, &addr_len); 
        n = recvfrom(fd, buffer, limit, 0, addr, &addr_len);
        if (n < 0) {
            perror("recvfrom() failed");
            return -1;  // failure
        }

        if (filter_message(addr, buffer, n)) {
            continue;  // early (succesful) exit
        }

        DEBUG(dump_sockaddr(stdout, addr, addr_len));

        fputs("Message: \n", stdout);
        hexdump(stdout, buffer, n);

        *((char *)buffer) = 0xEC;  // patch reply
        n = sendto(fd, buffer, n, 0, addr, addr_len);
        if (n < 0) {
            perror("sendto() failed");
            return -1;  // failure
        }

//        DEBUG(dump_sockaddr(stdout, addr, addr_len));
        fputs("Reply: \n", stdout);
        hexdump(stdout, buffer, n);
    }

    rv = close(fd);
    return rv;
}

int
main(int argc, char *argv[])
{
    // set new default protocol options
    proto_opt.family = AF_INET;
    proto_opt.sock_type = SOCK_DGRAM;
    proto_opt.ip_proto = IPPROTO_UDP;

    int rv = parse_args(&argc, argv);
    if (rv != 0) return rv;

    fputs(argv[0], stdout);
    print_proto_opt(stdout);

    rv = server(proto_buf, sizeof(proto_buf));

    return rv;
}
