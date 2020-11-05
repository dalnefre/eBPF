/*
 * proto.h -- network protocol support routines
 */
#ifndef _PROTO_H_
#define _PROTO_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <linux/if_packet.h>
//#include <linux/if_xdp.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>

//#define AF_XDP (44)

#define ETH_P_DALE (0xDa1e)

#define IPPROTO_DEFAULT (0x00)

typedef struct proto_opt {
    sa_family_t family;     // protocol family (AF_INET, AF_PACKET ...)
    int         sock_type;  // socket type (SOCK_STREAM, SOCK_DGRAM ...)
    unsigned    if_index;   // network interface index
    uint16_t    eth_proto;  // layer 3 protocol (ETH_P_IP, ETH_P_IPV6 ...)
    uint8_t     ip_proto;   // layer 4 protocol (IPPROTO_TCP, IPPROTO_UDP ...)
    uint32_t    ip_addr;    // IP address
    in_port_t   ip_port;    // IP port number
    int         filter;     // filter flags
    char        *ait;       // atomic information transfer
} proto_opt_t;

#define FILTER_NONE (0)
#define FILTER_IP   (1<<0)
#define FILTER_IPV6 (1<<1)
#define FILTER_ARP  (1<<2)

extern proto_opt_t proto_opt;  // global options

int create_socket();
int bind_socket(int fd);
struct sockaddr *clr_sockaddr(struct sockaddr_storage *store, socklen_t *len_ptr);
struct sockaddr *set_sockaddr(struct sockaddr_storage *store, socklen_t *len_ptr);
void dump_sockaddr(FILE *f, void *sockaddr, socklen_t len);
int filter_message(void *addr, void *data, size_t limit);
int get_link_status(int fd, int *status);
int find_mac_addr(int fd, void *mac_addr);
void print_mac_addr(FILE *f, char *label, void *mac_addr);
void print_proto_opt(FILE *f);
int parse_args(int *argc, char *argv[]);

#endif /* _PROTO_H_ */
