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

#define DEBUG(x) x /**/

#if 0
static octet_t message[] = {
    utf8, n_14,
    'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!', '\n',
};
#else
static octet_t message[] = {
//    array, n_6,
//    0x04, 0x86,
    octets, n_12,
//    n_1, n_2, p_int_0, n_2, 0x81, 0x00,
//    0x81, 0x82, 0x10, 0x82, 0x81, 0x00,
    0xAB, 0xCD, 0xEF, 0x23, 0x81, 0x00, 0xFF, 0xEE, 0x45, 0x67, 0x81, 0x00
};
#endif


size_t
create_message(void *buffer, size_t size)
{
    size_t offset = 0;

    size_t n = sizeof(message);
    memcpy(buffer + offset, message, n);
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

    struct sockaddr *addr = set_sockaddr(&address, &addr_len); 
    DEBUG(dump_sockaddr(stdout, addr, addr_len));

    n = sendto(fd, buffer, n, 0, addr, addr_len);
    if (n < 0) {
        perror("sendto() failed");
        return -1;  // failure
    }

    DEBUG(dump_sockaddr(stdout, addr, addr_len));

    fputs("Message: \n", stdout);
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
