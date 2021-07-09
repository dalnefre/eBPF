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
#include "link_msg.h"

#define DEBUG(x) x /**/

static octet_t proto_buf[1024];  // message-transfer buffer

int
transform(void *buffer, int n)
{
    if (n < sizeof(msg_hdr_t)) return -1;  // fail!
    msg_hdr_t *hdr = buffer;
    if (hdr->magic != MSG_MAGIC) return -1;  // fail!
    switch (hdr->op_code) {
        case OP_NONE: {
            break;
        }
        case OP_READ: {
            break;
        }
        case OP_WRITE: {
            break;
        }
        default: {
            hdr->magic = -1;
//            n = 1;
            break;
        }
    }
    return n;  // success -- number of characters in reply
}

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

        DEBUG(dump_sockaddr(stdout, addr, addr_len));
        fputs("Message: \n", stdout);
        hexdump(stdout, buffer, n);

        n = transform(buffer, n);
        if (n < 0) {
            fputs("Fail!\n", stdout);
            continue;  // skip reply on error
        }

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
