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
    .ip_proto = IPPROTO_DEFAULT,// layer 4 protocol
    .ip_addr = INADDR_LOOPBACK, // IP address
    .ip_port = 8888,            // IP port number
    .filter = FILTER_NONE,      // filter flags
    .ait = NULL,                // atomic information transfer
    .log = 1,                   // logging level
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
    socklen_t addr_len;
    int rv = -1;

    struct sockaddr *addr = set_sockaddr(&address, &addr_len); 
    rv = bind(fd, addr, addr_len);
    DEBUG(dump_sockaddr(stdout, addr, addr_len));
    return rv;
}

struct sockaddr *
clr_sockaddr(struct sockaddr_storage *store, socklen_t *len_ptr)
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
set_sockaddr(struct sockaddr_storage *store, socklen_t *len_ptr)
{
    struct sockaddr *sockaddr = clr_sockaddr(store, len_ptr);
    sockaddr->sa_family = proto_opt.family;

    if (proto_opt.family == AF_INET) {
        struct sockaddr_in *sin = (void *)store;

        sin->sin_addr.s_addr = htonl(proto_opt.ip_addr);
        sin->sin_port = htons(proto_opt.ip_port);

    } else if (proto_opt.family == AF_PACKET) {
        struct sockaddr_ll *sll = (void *)store;

        sll->sll_protocol = htons(proto_opt.eth_proto);
        sll->sll_ifindex = proto_opt.if_index;

    }
    return sockaddr;
}

void
dump_sockaddr(FILE *f, void *addr, socklen_t len)
{
    fputs("sockaddr: ", stdout);
    DEBUG(fputc('\n', stdout));
    DEBUG(hexdump(f, addr, len));

    if (proto_opt.family == AF_INET) {
        struct sockaddr_in *sin = addr;

        fprintf(f, "fam=%d, host=%u.%u.%u.%u, port=%d, len=%lu\n",
            sin->sin_family,
            ((uint8_t *) &(sin->sin_addr.s_addr))[0],
            ((uint8_t *) &(sin->sin_addr.s_addr))[1],
            ((uint8_t *) &(sin->sin_addr.s_addr))[2],
            ((uint8_t *) &(sin->sin_addr.s_addr))[3],
            ntohs(sin->sin_port),
            (unsigned long)len);

    } else if (proto_opt.family == AF_PACKET) {
        struct sockaddr_ll *sll = addr;

        fprintf(f, "fam=%d, proto=0x%04x, if=%d, len=%lu\n",
            sll->sll_family,
            ntohs(sll->sll_protocol),
            sll->sll_ifindex,
            (unsigned long)len);

    }
}

int
filter_message(void *addr, void *data, size_t limit)
{
    if (proto_opt.filter == FILTER_NONE) return 0;  // pass message
    if (proto_opt.family == AF_PACKET) {
        struct sockaddr_ll *sll = addr;

        if ((proto_opt.filter & FILTER_IP)
        &&  (ntohs(sll->sll_protocol) == ETH_P_IP)) {
            return 1;  // filter message
        }

        if ((proto_opt.filter & FILTER_IPV6)
        &&  (ntohs(sll->sll_protocol) == ETH_P_IPV6)) {
            return 1;  // filter message
        }

        if ((proto_opt.filter & FILTER_ARP)
        &&  (ntohs(sll->sll_protocol) == ETH_P_ARP)) {
            return 1;  // filter message
        }

    }
    return 0;  // pass message
}

int
get_link_status(int fd, int *status)
{
    struct ifreq ifr;
    int rv;

    ifr.ifr_addr.sa_family = proto_opt.family;
    ifr.ifr_ifindex = proto_opt.if_index;
    rv = ioctl(fd, SIOCGIFNAME, &ifr);
    if (rv < 0) return rv;
    struct ethtool_value value = {
        .cmd = ETHTOOL_GLINK,
    };
    ifr.ifr_data = ((char *) &value);
    rv = ioctl(fd, SIOCETHTOOL, &ifr);
    if (rv < 0) return rv;
    *status = value.data;
    return 0;
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

void
print_mac_addr(FILE *f, char *label, void *mac_addr)
{
    uint8_t *bp = mac_addr;

    fprintf(f, "%s%02x:%02x:%02x:%02x:%02x:%02x\n",
        label, bp[0], bp[1], bp[2], bp[3], bp[4], bp[5]);
}

void
print_proto_opt(FILE *f)
{

    // Protocol Family
    switch (proto_opt.family) {
        case AF_INET: { fputs(" AF_INET", f); break; }
        case AF_INET6: { fputs(" AF_INET6", f); break; }
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

    // Filter Flags
    if (proto_opt.filter & FILTER_IP) { fputs(" FILTER_IP", f); }
    if (proto_opt.filter & FILTER_IPV6) { fputs(" FILTER_IPV6", f); }
    if (proto_opt.filter & FILTER_ARP) { fputs(" FILTER_ARP", f); }

    // IP Protocol (AF_INET only)
    if (proto_opt.family == AF_INET) {
        switch (proto_opt.ip_proto) {
            case IPPROTO_UDP: { fputs(" IPPROTO_UDP", f); break; }
            case IPPROTO_TCP: { fputs(" IPPROTO_TCP", f); break; }
            case IPPROTO_RAW: { fputs(" IPPROTO_RAW", f); break; }
#ifdef IPPROTO_DEFAULT
            case IPPROTO_DEFAULT: { fputs(" IPPROTO_DEFAULT", f); break; }
#endif /* IPPROTO_DEFAULT */
        }
    }

    // IP Host/Port (AF_INET only)
    if (proto_opt.family == AF_INET) {
        struct in_addr addr = {
            .s_addr = htonl(proto_opt.ip_addr),
        };

        fprintf(f, " %s:%d", inet_ntoa(addr), proto_opt.ip_port);
    }

    // Network Interface (AF_PACKET only)
    if (proto_opt.family == AF_PACKET) {
        if (proto_opt.if_index == 0) {
            fputs(" if=*", f);
        } else {
            fprintf(f, " if=%d", proto_opt.if_index);
        }
    }

    // Atomic Information Transfer
    if (proto_opt.ait) {
        fprintf(f, " ait=\"%s\"", proto_opt.ait);
    }

    // Log Output Level
    fprintf(f, " log=%d", proto_opt.log);

    fputc('\n', f);
}

int
parse_args(int *argc, char *argv[])
{
    while ((--*argc) > 0) {
        char *arg = *++argv;

        if (strcmp(arg, "UDP") == 0) {
            proto_opt.family = AF_INET;
            proto_opt.sock_type = SOCK_DGRAM;
            proto_opt.ip_proto = IPPROTO_UDP;
            continue;  // next arg
        } else if (strcmp(arg, "TCP") == 0) {
            proto_opt.family = AF_INET;
            proto_opt.sock_type = SOCK_STREAM;
            proto_opt.ip_proto = IPPROTO_TCP;
            continue;  // next arg
        } else if (strcmp(arg, "ETH") == 0) {
            proto_opt.family = AF_PACKET;
            proto_opt.sock_type = SOCK_RAW;
            proto_opt.eth_proto = ETH_P_ALL;
            continue;  // next arg
        }

        if (strcmp(arg, "IP") == 0) {
            proto_opt.family = AF_INET;
            proto_opt.eth_proto = ETH_P_IP;
            continue;  // next arg
        } else if (strcmp(arg, "IPV4") == 0) {
            proto_opt.family = AF_INET;
            proto_opt.eth_proto = ETH_P_IP;
            continue;  // next arg
        } else if (strcmp(arg, "IPv4") == 0) {
            proto_opt.family = AF_INET;
            proto_opt.eth_proto = ETH_P_IP;
            continue;  // next arg
        } else if (strcmp(arg, "IPV6") == 0) {
            proto_opt.family = AF_INET6;
            proto_opt.eth_proto = ETH_P_IPV6;
            continue;  // next arg
        } else if (strcmp(arg, "IPv6") == 0) {
            proto_opt.family = AF_INET6;
            proto_opt.eth_proto = ETH_P_IPV6;
            continue;  // next arg
        }

        if (strcmp(arg, "AF_INET") == 0) {
            proto_opt.family = AF_INET;
            continue;  // next arg
        } else if (strcmp(arg, "AF_INET6") == 0) {
            proto_opt.family = AF_INET6;
            continue;  // next arg
        } else if (strcmp(arg, "AF_PACKET") == 0) {
            proto_opt.family = AF_PACKET;
            continue;  // next arg
#ifdef AF_XDP
        } else if (strcmp(arg, "AF_XDP") == 0) {
            proto_opt.family = AF_XDP;
            continue;  // next arg
#endif /* AF_XDP */
        } else if (strncmp(arg, "AF_", 3) == 0) {
            fprintf(stderr, "%s not supported.\n", arg);
            return -1;
        }

        if (strcmp(arg, "SOCK_DGRAM") == 0) {
            proto_opt.sock_type = SOCK_DGRAM;
            continue;  // next arg
        } else if (strcmp(arg, "SOCK_STREAM") == 0) {
            proto_opt.sock_type = SOCK_STREAM;
            continue;  // next arg
        } else if (strcmp(arg, "SOCK_RAW") == 0) {
            proto_opt.sock_type = SOCK_RAW;
            continue;  // next arg
        } else if (strncmp(arg, "SOCK_", 5) == 0) {
            fprintf(stderr, "%s not supported.\n", arg);
            return -1;
        }

        if (strcmp(arg, "ETH_P_IP") == 0) {
            proto_opt.eth_proto = ETH_P_IP;
            continue;  // next arg
        } else if (strcmp(arg, "ETH_P_IPV6") == 0) {
            proto_opt.eth_proto = ETH_P_IPV6;
            continue;  // next arg
        } else if (strcmp(arg, "ETH_P_ALL") == 0) {
            proto_opt.eth_proto = ETH_P_ALL;
            continue;  // next arg
#ifdef ETH_P_DALE
        } else if (strcmp(arg, "ETH_P_DALE") == 0) {
            proto_opt.eth_proto = ETH_P_DALE;
            continue;  // next arg
#endif /* ETH_P_DALE */
        } else if (strncmp(arg, "ETH_", 4) == 0) {
            fprintf(stderr, "%s not supported.\n", arg);
            return -1;
        }

        if (strcmp(arg, "FILTER_NONE") == 0) {
            proto_opt.filter = FILTER_NONE;
            continue;  // next arg
        } else if (strcmp(arg, "FILTER_IP") == 0) {
            proto_opt.filter |= FILTER_IP;
            continue;  // next arg
        } else if (strcmp(arg, "FILTER_IPV6") == 0) {
            proto_opt.filter |= FILTER_IPV6;
            continue;  // next arg
        } else if (strcmp(arg, "FILTER_ARP") == 0) {
            proto_opt.filter |= FILTER_ARP;
            continue;  // next arg
        } else if (strncmp(arg, "FILTER_", 4) == 0) {
            fprintf(stderr, "%s not supported.\n", arg);
            return -1;
        }

        if (strcmp(arg, "IPPROTO_UDP") == 0) {
            proto_opt.ip_proto = IPPROTO_UDP;
            continue;  // next arg
        } else if (strcmp(arg, "IPPROTO_TCP") == 0) {
            proto_opt.ip_proto = IPPROTO_TCP;
            continue;  // next arg
        } else if (strcmp(arg, "IPPROTO_RAW") == 0) {
            proto_opt.ip_proto = IPPROTO_RAW;
            continue;  // next arg
#ifdef IPPROTO_DEFAULT
        } else if (strcmp(arg, "IPPROTO_DEFAULT") == 0) {
            proto_opt.ip_proto = IPPROTO_DEFAULT;
            continue;  // next arg
#endif /* IPPROTO_DEFAULT */
        } else if (strncmp(arg, "IPPROTO_", 8) == 0) {
            fprintf(stderr, "%s not supported.\n", arg);
            return -1;
        }

        if (strcmp(arg, "if=*") == 0) {
            proto_opt.if_index = 0;
            continue;  // next arg
        } else if (strncmp(arg, "if=", 3) == 0) {
            unsigned index = atoi(arg + 3);
            if (!index) {
                index = if_nametoindex(arg + 3);
            }
            proto_opt.if_index = index;
            continue;  // next arg
        }

        if (strncmp(arg, "ait=", 4) == 0) {
            proto_opt.ait = arg + 4;
            continue;  // next arg
        }

        if (strncmp(arg, "log=", 4) == 0) {
            proto_opt.log = atoi(arg + 4);
            continue;  // next arg
        }

        char *p = strrchr(arg, ':');
        if (p) {
            *p++ = '\0';  // split arg at ':'
            proto_opt.ip_port = atoi(p);
        }
        if (strcmp(arg, "") == 0) {
            // use default address
            continue;  // next arg
        } else if (strcmp(arg, "INADDR_ANY") == 0) {
            proto_opt.ip_addr = INADDR_ANY;
            continue;  // next arg
        } else if (strcmp(arg, "INADDR_LOOPBACK") == 0) {
            proto_opt.ip_addr = INADDR_LOOPBACK;
            continue;  // next arg
        } else if (strcmp(arg, "INADDR_BROADCAST") == 0) {
            proto_opt.ip_addr = INADDR_BROADCAST;
            continue;  // next arg
        }
        struct in_addr addr = {
            .s_addr = htonl(proto_opt.ip_addr),
        };
        if (inet_aton(arg, &addr) <= 0) {
            fprintf(stderr, "bad address %s\n", arg);
            return -1;
        }
        proto_opt.ip_addr = ntohl(addr.s_addr);

    }
    return 0;
}
