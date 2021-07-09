/*
 * link_cli.c -- link protocol lab client
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

typedef struct req_none {
    msg_hdr_t   hdr;            // message header
    char        text[14];       // message text
} req_none_t;

#if 0
static octet_t message[] = {
    utf8, n_14,
    'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!', '\n',
};
#else
static req_none_t message = {
    { MSG_MAGIC, OP_NONE, 0, 0 },
    { 'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!', '\n' }
};
#endif


size_t
create_message(void *buffer, size_t size)
{
    size_t offset = 0;

    size_t n = sizeof(message);
    memcpy(buffer + offset, (void *)&message, n);
    offset += n;
    return offset;
}

static octet_t proto_buf[1024];  // message-transfer buffer

int
client(void *buffer, size_t limit)
{
    int fd, rv, n; 
    struct sockaddr_storage address;
    socklen_t addr_len;

    fd = create_socket();
    if (fd < 0) {
        perror("create_socket() failed");
        return -1;  // failure
    }

    n = create_message(buffer, limit);
    if (n <= 0) {
        fprintf(stderr, "exceeded message buffer size of %zu\n", limit);
        return -1;  // failure
    }

    struct sockaddr *to_addr = set_sockaddr(&address, &addr_len); 
    DEBUG(dump_sockaddr(stdout, to_addr, addr_len));

    n = sendto(fd, buffer, n, 0, to_addr, addr_len);
    if (n < 0) {
        perror("sendto() failed");
        return -1;  // failure
    }

//    DEBUG(dump_sockaddr(stdout, to_addr, addr_len));
    fputs("Message: \n", stdout);
    hexdump(stdout, buffer, n);

    fputs("Awaiting reply...\n", stdout);
    memset(buffer, 0, limit);  // clear buffer
    struct sockaddr *from_addr = clr_sockaddr(&address, &addr_len); 
    n = recvfrom(fd, buffer, limit, 0, from_addr, &addr_len);
    if (n < 0) {
        perror("recvfrom() failed");
        return -1;  // failure
    }

    DEBUG(dump_sockaddr(stdout, to_addr, addr_len));
    fputs("Reply: \n", stdout);
    hexdump(stdout, buffer, n);

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

    rv = client(proto_buf, sizeof(proto_buf));

    return rv;
}
