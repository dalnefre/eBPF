/*
 * client.c -- network protocol lab client
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

static octet_t eth_remote[ETH_ALEN] =  { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static octet_t eth_local[ETH_ALEN] =   { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

size_t
create_message(void *buffer, size_t size)
{
    size_t offset = 0;

    if ((proto_opt.family == AF_PACKET)
    &&  (proto_opt.sock_type == SOCK_RAW)) {
        // add Ethernet header for RAW PACKET
        if (size < ETH_HLEN) return 0;  // buffer too small
        struct ethhdr *hdr = buffer;
        memcpy(hdr->h_dest, eth_remote, ETH_ALEN);
        memcpy(hdr->h_source, eth_local, ETH_ALEN);
        hdr->h_proto = htons(proto_opt.eth_proto);
        offset += ETH_HLEN;
    }
    size_t n = sizeof(message);
    memcpy(buffer + offset, message, n);
    offset += n;
    return offset;
}

int
send_message(int fd, void *buffer, size_t size)
{
    struct sockaddr_storage address;
    socklen_t addr_len;
    int n; 

    struct sockaddr *addr = set_sockaddr(&address, &addr_len); 
    DEBUG(dump_sockaddr(stdout, addr, addr_len));

    n = sendto(fd, buffer, size, 0, addr, addr_len);
    if (n < 0) return n;  // sendto error

    DEBUG(dump_sockaddr(stdout, addr, addr_len));

    fputs("Message: \n", stdout);
    hexdump(stdout, buffer, n);

    return n;
}

static octet_t proto_buf[256];  // message-transfer buffer

int
client()
{
    int fd, rv;
    size_t n;

    fd = create_socket();
    if (fd < 0) {
        perror("create_socket() failed");
        return -1;  // failure
    }

    if (proto_opt.family == AF_PACKET) {
        rv = find_mac_addr(fd, eth_local);
        if (rv < 0) {
            perror("find_mac_addr() failed");
            return -1;  // failure
        }
    }

    n = create_message(proto_buf, sizeof(proto_buf));
    if (n <= 0) {
        fprintf(stderr, "exceeded message buffer size of %zu\n",
            sizeof(proto_buf));
        return -1;  // failure
    }

    rv = send_message(fd, proto_buf, n);
    if (rv < 0) {
        perror("send_message() failed");
        return -1;  // failure
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

    rv = client();

    return rv;
}
