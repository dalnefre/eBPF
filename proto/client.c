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

static char *message = "Hello, World!\n";

static BYTE eth_remote[ETH_ALEN] =  { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static BYTE eth_local[ETH_ALEN] =   { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

size_t
create_message(void *buffer, size_t size)
{
    size_t offset = 0;
    size_t n;

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
    n =  encode_cstr(buffer + offset, size - offset, message);
    if (n <= 0) return 0;  // error
    offset += n;
    return offset;
}

int
send_message(int fd, void *buffer, size_t size)
{
    struct sockaddr_storage address;
    size_t addr_len;
    int n; 

    struct sockaddr *addr = set_sockaddr(&address, &addr_len); 
    DEBUG(dump_sockaddr(stdout, addr, addr_len));

    n = sendto(fd, buffer, size, 0, addr, addr_len);
    DEBUG(dump_sockaddr(stdout, addr, addr_len));
    return n;
}

static BYTE proto_buf[256];  // message-transfer buffer

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
        fprintf(stderr, "exceeded message buffer size of %u\n",
            sizeof(proto_buf));
        return -1;  // failure
    }

    rv = send_message(fd, proto_buf, n);
    if (rv < 0) {
        perror("send_message() failed");
        return -1;  // failure
    }
    fputs("Message: \n", stdout);
    hexdump(stdout, proto_buf, n);

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
