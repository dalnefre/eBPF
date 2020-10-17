/*
 * proto.c -- network protocol support routines
 */
#include "proto.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

#define DEBUG(x)   /**/

proto_opt_t proto_opt = {   // global options
    .family = AF_INET,          // protocol family
    .sock_type = SOCK_DGRAM,    // socket type
    .if_index = 0,              // network interface index
    .eth_proto = ETH_P_IP,      // layer 3 protocol
    .ip_proto = 0,              // layer 4 protocol
    .ip_addr = INADDR_ANY,      // IP address
    .ip_port = 8080,            // IP port number
};

int
create_socket()
{
    int fd = -1;

    if (proto_opt.family == AF_INET) {
        fd = socket(
            proto_opt.family,
            proto_opt.sock_type,
            proto_opt.ip_proto
        );
    } else if (proto_opt.family == AF_PACKET) {
        fd = socket(
            proto_opt.family,
            proto_opt.sock_type,
            htons(proto_opt.eth_proto)
        );
    } else {
        return -1;  // unsupported address family
    }
    return fd;
}

int
bind_socket(int fd)
{
    struct sockaddr_storage address;
    size_t addr_len;
    int rv = -1;

    struct sockaddr *addr = set_sockaddr(&address, &addr_len); 
    rv = bind(fd, addr, addr_len);
    DEBUG(dump_sockaddr(stdout, addr, addr_len));
    return rv;
}

int
find_mac_addr(int fd, void *mac_addr)
{
    struct ifreq ifr;
    int rv;

    ifr.ifr_addr.sa_family = proto_opt.family;
    ifr.ifr_ifindex = proto_opt.if_index;
    rv = ioctl(fd, SIOCGIFNAME, &ifr);
    if (rv < 0) return rv;
    rv = ioctl(fd, SIOCGIFHWADDR, &ifr);
    if (rv < 0) return rv;
    memcpy(mac_addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    return 0;
}

struct sockaddr *
clr_sockaddr(struct sockaddr_storage *store, size_t *len_ptr)
{
    if (proto_opt.family == AF_INET) {
        *len_ptr = sizeof(struct sockaddr_in);
    } else if (proto_opt.family == AF_PACKET) {
        *len_ptr = sizeof(struct sockaddr_ll);
    } else {
        *len_ptr = sizeof(struct sockaddr_storage);
    }
    memset(store, 0, *len_ptr); 
    return (struct sockaddr *)store;
}

struct sockaddr *
set_sockaddr(struct sockaddr_storage *store, size_t *len_ptr)
{
    struct sockaddr *sockaddr = clr_sockaddr(store, len_ptr);
    sockaddr->sa_family = proto_opt.family;

    if (proto_opt.family == AF_INET) {
        struct sockaddr_in *addr = (void *)store;

        addr->sin_addr.s_addr = htonl(proto_opt.ip_addr);
        addr->sin_port = htons(proto_opt.ip_port);

    } else if (proto_opt.family == AF_PACKET) {
        struct sockaddr_ll *addr = (void *)store;

        addr->sll_protocol = htons(proto_opt.eth_proto);
        addr->sll_ifindex = proto_opt.if_index;

    }
    return sockaddr;
}

void
dump_sockaddr(FILE *f, void *ptr, size_t len)
{
    fputs("sockaddr: ", stdout);
    DEBUG(fputc('\n', stdout));
    DEBUG(hexdump(f, ptr, len));

    if (proto_opt.family == AF_INET) {
        struct sockaddr_in *addr = ptr;

        fprintf(f, "fam=%d, addr=%u.%u.%u.%u, port=%d, len=%zu\n",
            addr->sin_family,
            ((uint8_t *) &(addr->sin_addr.s_addr))[0],
            ((uint8_t *) &(addr->sin_addr.s_addr))[1],
            ((uint8_t *) &(addr->sin_addr.s_addr))[2],
            ((uint8_t *) &(addr->sin_addr.s_addr))[3],
            ntohs(addr->sin_port),
            len);

    } else if (proto_opt.family == AF_PACKET) {
        struct sockaddr_ll *addr = ptr;

        fprintf(f, "fam=%d, proto=0x%04x, if=%d, len=%zu\n",
            addr->sll_family,
            ntohs(addr->sll_protocol),
            addr->sll_ifindex,
            len);

    }
}

void
print_proto_opt(FILE *f)
{

    // Protocol Family
    switch (proto_opt.family) {
        case AF_INET: { fputs(" AF_INET", f); break; }
        case AF_PACKET: { fputs(" AF_PACKET", f); break; }
#ifdef AF_XDP
        case AF_XDP: { fputs(" AF_XDP", f); break; }
#endif /* AF_XDP */
    }

    // Socket Type
    switch (proto_opt.sock_type) {
        case SOCK_DGRAM: { fputs(" SOCK_DGRAM", f); break; }
        case SOCK_STREAM: { fputs(" SOCK_STREAM", f); break; }
        case SOCK_RAW: { fputs(" SOCK_RAW", f); break; }
    }

    // Ethertype
    switch (proto_opt.eth_proto) {
        case ETH_P_IP: { fputs(" ETH_P_IP", f); break; }
        case ETH_P_IPV6: { fputs(" ETH_P_IPV6", f); break; }
        case ETH_P_ALL: { fputs(" ETH_P_ALL", f); break; }
#ifdef ETH_P_DALE
        case ETH_P_DALE: { fputs(" ETH_P_DALE", f); break; }
#endif /* ETH_P_DALE */
    }

    // IP Protocol (AF_INET only)
    if (proto_opt.family == AF_INET) {
        switch (proto_opt.ip_proto) {
            case IPPROTO_UDP: { fputs(" IPPROTO_UDP", f); break; }
            case IPPROTO_TCP: { fputs(" IPPROTO_TCP", f); break; }
            case IPPROTO_RAW: { fputs(" IPPROTO_RAW", f); break; }
        }
    }

    // IP Protocol (AF_INET only)
    if (proto_opt.family == AF_INET) {
        if (proto_opt.ip_port == INADDR_ANY) {
            fputs(" port=*", f);
        } else {
            fprintf(f, " port=%d\n", proto_opt.ip_port);
        }
    }

    // Network Interface (AF_PACKET only)
    if (proto_opt.family == AF_PACKET) {
        if (proto_opt.if_index == 0) {
            fputs(" if=*", f);
        } else {
            fprintf(f, " if=%d\n", proto_opt.if_index);
        }
    }

}

int
parse_args(int *argc, char *argv[])
{
    while ((--*argc) > 0) {
        char *arg = *++argv;

        if (strcmp(arg, "AF_INET") == 0) {
            proto_opt.family = AF_INET;
        } else if (strcmp(arg, "AF_PACKET") == 0) {
            proto_opt.family = AF_PACKET;
#ifdef AF_XDP
        } else if (strcmp(arg, "AF_XDP") == 0) {
            proto_opt.family = AF_XDP;
#endif /* AF_XDP */
        } else if (strncmp(arg, "AF_", 3) == 0) {
            fprintf(stderr, "%s not supported.\n", arg);
            return -1;
        }

        if (strcmp(arg, "SOCK_DGRAM") == 0) {
            proto_opt.sock_type = SOCK_DGRAM;
        } else if (strcmp(arg, "SOCK_STREAM") == 0) {
            proto_opt.sock_type = SOCK_STREAM;
        } else if (strcmp(arg, "SOCK_RAW") == 0) {
            proto_opt.sock_type = SOCK_RAW;
        } else if (strncmp(arg, "SOCK_", 5) == 0) {
            fprintf(stderr, "%s not supported.\n", arg);
            return -1;
        }

        if (strcmp(arg, "ETH_P_IP") == 0) {
            proto_opt.eth_proto = ETH_P_IP;
        } else if (strcmp(arg, "IP") == 0) {
            proto_opt.eth_proto = ETH_P_IP;
        } else if (strcmp(arg, "IPV4") == 0) {
            proto_opt.eth_proto = ETH_P_IP;
        } else if (strcmp(arg, "IPv4") == 0) {
            proto_opt.eth_proto = ETH_P_IP;
        } else if (strcmp(arg, "ETH_P_IPV6") == 0) {
            proto_opt.eth_proto = ETH_P_IPV6;
        } else if (strcmp(arg, "IPV6") == 0) {
            proto_opt.eth_proto = ETH_P_IPV6;
        } else if (strcmp(arg, "IPv6") == 0) {
            proto_opt.eth_proto = ETH_P_IPV6;
        } else if (strcmp(arg, "ETH_P_ALL") == 0) {
            proto_opt.eth_proto = ETH_P_ALL;
#ifdef ETH_P_DALE
        } else if (strcmp(arg, "ETH_P_DALE") == 0) {
            proto_opt.eth_proto = ETH_P_DALE;
#endif /* ETH_P_DALE */
        } else if (strncmp(arg, "ETH_", 4) == 0) {
            fprintf(stderr, "%s not supported.\n", arg);
            return -1;
        }

        if (strcmp(arg, "IPPROTO_UDP") == 0) {
            proto_opt.ip_proto = IPPROTO_UDP;
        } else if (strcmp(arg, "UDP") == 0) {
            proto_opt.ip_proto = IPPROTO_UDP;
        } else if (strcmp(arg, "IPPROTO_TCP") == 0) {
            proto_opt.ip_proto = IPPROTO_TCP;
        } else if (strcmp(arg, "TCP") == 0) {
            proto_opt.ip_proto = IPPROTO_TCP;
        } else if (strcmp(arg, "IPPROTO_RAW") == 0) {
            proto_opt.ip_proto = IPPROTO_RAW;
        } else if (strncmp(arg, "IPPROTO_", 8) == 0) {
            fprintf(stderr, "%s not supported.\n", arg);
            return -1;
        }

        if (strcmp(arg, "port=INADDR_ANY") == 0) {
            proto_opt.ip_port = INADDR_ANY;
        } else if (strcmp(arg, "port=ANY") == 0) {
            proto_opt.ip_port = INADDR_ANY;
        } else if (strcmp(arg, "port=*") == 0) {
            proto_opt.ip_port = INADDR_ANY;
        } else if (strncmp(arg, "port=", 5) == 0) {
            int port = atoi(arg + 5);
            DEBUG(printf("port: %d\n", port));
            proto_opt.ip_port = port;
        }

        if (strcmp(arg, "if=*") == 0) {
            proto_opt.if_index = 0;
        } else if (strncmp(arg, "if=", 3) == 0) {
            unsigned index = atoi(arg + 3);
            if (!index) {
                index = if_nametoindex(arg + 3);
            }
            proto_opt.if_index = index;
        }

    }
    return 0;
}
