/*
 * link.c -- Liveness and AIT link protocols
 */
#include "proto.h"
#include "util.h"
#include "code.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <inttypes.h>

#define DEBUG(x)   /**/

#define PERMISSIVE   0  // allow non-protocol packets to pass through
#define LOG_LEVEL    2  // log level (0=none, 1=AIT, 2=protocol, 3=hexdump)
#define PACKET_LIMIT 3  // halt ping/pong after limited number of packets

#ifndef ETH_P_DALE
#define ETH_P_DALE (0xDa1e)
#endif

typedef enum {
    Init,       // = 0
    Ping,       // = 1
    Pong,       // = 2
    Got_AIT,    // = 3
    Ack_AIT,    // = 4
    Ack_Ack,    // = 5
    Proceed,    // = 6
    Error       // = 7
} protocol_t;

typedef struct link_state {
    __u8        frame[64];      // transport frame
    protocol_t  i;              // local link protocol state
    protocol_t  u;              // remote link protocol state
    __u8        seq;            // sequence number {0,1}
    __u8        len;            // payload length (< 127)
    __u32       link_flags;     // flags controller by link
    __u32       user_flags;     // flags controller by user
} link_state_t;

#define LF_FULL (((__u32)1)<<0) // outbound AIT full
#define LF_VALD (((__u32)1)<<1) // inbound AIT valid
#define LF_ENTL (((__u32)1)<<2) // link entangled
#define LF_SEND (((__u32)1)<<3) // link sending AIT
#define LF_RECV (((__u32)1)<<4) // link receiving AIT

#define UF_FULL (((__u32)1)<<0) // inbound AIT full
#define UF_VALD (((__u32)1)<<1) // outbound AIT valid
#define UF_STOP (((__u32)1)<<2) // run=1, stop=0

//static octet_t proto_buf[256];  // message-transfer buffer
static octet_t proto_buf[64];  // message-transfer buffer
//static octet_t proto_buf[ETH_ZLEN];  // message-transfer buffer
//static octet_t proto_buf[ETH_MIN_MTU];  // message-transfer buffer

static octet_t proto_init[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // dst_mac = broadcast
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // src_mac = eth_local
    0xDa, 0x1e,                          // protocol ethertype
    INT2SMOL(0),                         // state = {s:0, i:0, u:0}
    INT2SMOL(0),                         // payload len = 0
};
static octet_t *eth_remote = &proto_init[0 * ETH_ALEN];
static octet_t *eth_local = &proto_init[1 * ETH_ALEN];


link_state_t link_state[16];    // link state by if_index

link_state_t *
get_link_state(int if_index)
{
    if ((if_index > 0)
    ||  (if_index < (sizeof(link_state) / sizeof(link_state_t)))) {
        return &link_state[if_index];
    }
    return NULL;
}

int
send_message(int fd, void *data, size_t size, struct timespec *ts)
{
    struct sockaddr_storage address;
    socklen_t addr_len;
    int n;

    struct sockaddr *addr = set_sockaddr(&address, &addr_len); 
    DEBUG(dump_sockaddr(stdout, addr, addr_len));

    clock_gettime(CLOCK_REALTIME, ts);

    n = sendto(fd, data, size, 0, addr, addr_len);
    if (n < 0) return n;  // sendto error

    DEBUG(dump_sockaddr(stdout, addr, addr_len));

    if (proto_opt.log >= 3) {
        fprintf(stdout, "%zu.%09ld Message[%d] --> \n",
            (size_t)ts->tv_sec, (long)ts->tv_nsec, n);
        hexdump(stdout, data, size);
    }

    return n;
}

int
recv_message(int fd, void *data, size_t limit, struct timespec *ts)
{
    struct sockaddr_storage address;
    socklen_t addr_len;
    int n;

    struct sockaddr *addr = clr_sockaddr(&address, &addr_len); 
    n = recvfrom(fd, data, limit, 0, addr, &addr_len);
    if (n < 0) return n;  // recvfrom error

    clock_gettime(CLOCK_REALTIME, ts);

    DEBUG(dump_sockaddr(stdout, addr, addr_len));

    if (proto_opt.log >= 3) {
        fprintf(stdout, "%zu.%09ld Message[%d] <-- \n",
            (size_t)ts->tv_sec, (long)ts->tv_nsec, n);
        hexdump(stdout, data, (n < 0 ? limit : n));
    }

    return n;
}

enum xdp_action {
    XDP_ABORTED = 0,
    XDP_DROP,
    XDP_PASS,
    XDP_TX,
    XDP_REDIRECT,
};

/*
link_state {
    __u8        frame[64];      // transport frame
    protocol_t  i;              // local link protocol state
    protocol_t  u;              // remote link protocol state
    __u8        seq;            // sequence number {0,1}
    __u8        len;            // payload length (< 127)
    __u32       link_flags;     // flags controller by link
    __u32       user_flags;     // flags controller by user
}
*/

#define GET_FLAG(lval,rval) !!((lval) & (rval))
#define SET_FLAG(lval,rval) (lval) |= (rval)
#define CLR_FLAG(lval,rval) (lval) &= ~(rval)

static int
on_frame_recv(__u8 *data, __u8 *end, link_state_t *link)
{
    __u8 b;
    __u8 s;
    protocol_t i;
    protocol_t u;
    __u8 len;

    // parse protocol state
    b = data[ETH_HLEN + 0];
    if ((b & 0200) == 0) return XDP_DROP;  // bad format
    s = (b & 0100) >> 6;
    i = (b & 0070) >> 3;
    u = (b & 0007);
    if (proto_opt.log >= 2) {
        printf("  #%d %d,%d <--\n", s, i, u);
    }

    // parse payload length
    b = data[ETH_HLEN + 1];
    if ((b & 0200) == 0) return XDP_DROP;  // bad format
    len = (b & 0177);

    // handle initialization
    link->i = u;
    if (i == Init) {
        __builtin_memcpy(eth_remote, data + ETH_ALEN, ETH_ALEN);
        __builtin_memcpy(link->frame, eth_remote, ETH_ALEN);
        if (proto_opt.log >= 1) {
            print_mac_addr(stdout, "eth_remote = ", eth_remote);
//            print_mac_addr(stdout, "eth_local = ", eth_local);
        }
    }
    if (u == Init) {
        if (len != 0) return XDP_DROP;  // unexpected payload
        link->u = Ping;
        link->seq = 0;
    }

/*  --FIXME--
    // check src/dst mac addrs
*/

    // check sequence number
    if ((link->seq & 1) != s) {
        if (proto_opt.log >= 2) {
            printf("wrong seq #. expect=0x%x, actual=0x%x\n",
                link->seq, s);
        }
/*  --FIXME--
        return XDP_TX;  // re-send last frame
*/
    }

    // protocol state machine
    switch (u) {
        case Init : break;  // already handled above...
        case Ping : {
            if (len != 0) return XDP_DROP;  // unexpected payload
            link->u = Pong;
            break;
        }
        case Pong : {
            if (len != 0) return XDP_DROP;  // unexpected payload
            link->u = Ping;
            break;
        }
        default: return XDP_DROP;  // bad state
    }

    // construct reply frame
    link->seq += 1;
    b = 0200
      | (link->seq & 1) << 6
      | link->i << 3
      | link->u;
    link->frame[ETH_HLEN + 0] = b;
    b = 0200 | link->len;
    link->frame[ETH_HLEN + 1] = b;
    if (proto_opt.log >= 2) {
        printf("  #%d %d,%d -->\n", link->seq, link->i, link->u);
    }

    return XDP_TX;  // send updated frame out on same interface
}

int
xdp_filter(void *data, void *end, link_state_t *link)
{
    size_t data_len = end - data;

    if (data + ETH_ZLEN > end) {
        if (proto_opt.log >= 2) {
            printf("frame too small. expect=%zu, actual=%zu\n",
                (size_t)ETH_ZLEN, data_len);
        }
        return XDP_DROP;  // frame too small
    }
    struct ethhdr *eth = data;
    __u16 eth_proto = ntohs(eth->h_proto);
    if (eth_proto != ETH_P_DALE) {
        if (proto_opt.log >= 2) {
            printf("wrong protocol. expect=0x%x, actual=0x%x\n",
                ETH_P_DALE, eth_proto);
        }
#if PERMISSIVE
        return XDP_PASS;  // pass on to default network stack
#else
        return XDP_DROP;  // wrong protocol
#endif
    }

    int rc = on_frame_recv(data, end, link);
    if (proto_opt.log >= 3) {
        printf("proto=0x%x len=%zu rc=%d\n", eth_proto, data_len, rc);
    }
#if PACKET_LIMIT
    if (link->seq > PACKET_LIMIT) return XDP_ABORTED;  // halt ping/pong!
#endif

    return rc;
}

int
send_init(int fd, link_state_t *link)
{
    struct timespec ts;
    int n;

    link->i = Init;
    link->u = Init;
    link->seq = 0;
    link->link_flags = 0;
    link->user_flags = 0;
    memset(link->frame, null, sizeof(link->frame)); 
    memcpy(link->frame, proto_init, sizeof(proto_init));

    n = send_message(fd, link->frame, ETH_ZLEN, &ts);
    if (n <= 0) {
        perror("send_message() failed");
    } else if (proto_opt.log >= 2) {
        printf("  #%d %d,%d -->\n", link->seq, link->i, link->u);
    }
    return n;
}

int
server(int fd, link_state_t *link)
{
    struct timespec ts;
    int rc, n;

    if (send_init(fd, link) < 0) return -1;  // failure

    for (;;) {

        n = recv_message(fd, proto_buf, sizeof(proto_buf), &ts);
        if (n <= 0) {
            perror("recv_message() failed");
            return -1;  // failure
        }

        rc = xdp_filter(proto_buf, proto_buf + n, link);
        if (rc == XDP_ABORTED) {
            perror("XDP_ABORTED");
            return -1;  // failure
        }

        if (rc == XDP_TX) {
            n = send_message(fd, link->frame, ETH_ZLEN, &ts);
            if (n <= 0) {
                perror("send_message() failed");
                return -1;  // failure
            }
        }

    }
}

int
main(int argc, char *argv[])
{
    int rv, fd;

    // set new default protocol options
    proto_opt.family = AF_PACKET;
    proto_opt.sock_type = SOCK_RAW;
    proto_opt.eth_proto = ETH_P_DALE;
    proto_opt.log = LOG_LEVEL;

    rv = parse_args(&argc, argv);
    if (rv != 0) return rv;

    fputs(argv[0], stdout);
    print_proto_opt(stdout);

    if (proto_opt.if_index <= 0) {
        fprintf(stderr, "usage: %s if=<interface>\n", argv[0]);
        return 1;
    }

    // determine real-time resolution
    struct timespec ts;
    if (clock_getres(CLOCK_REALTIME, &ts) < 0) {
        perror("clock_getres() failed");
        return -1;
    }
    if (proto_opt.log >= 3) {
        printf("CLOCK_REALTIME resolution %zu.%09ld\n",
            (size_t)ts.tv_sec, (long)ts.tv_nsec);
    }

    fd = create_socket();
    if (fd < 0) {
        perror("create_socket() failed");
        return -1;
    }

    rv = find_mac_addr(fd, eth_local);
    if (rv < 0) {
        perror("find_mac_addr() failed");
        return -1;  // failure
    }
    if (proto_opt.log >= 1) {
//        print_mac_addr(stdout, "eth_remote = ", eth_remote);
        print_mac_addr(stdout, "eth_local = ", eth_local);
    }

    rv = bind_socket(fd);
    if (rv < 0) {
        perror("bind_socket() failed");
        return -1;  // failure
    }

    int status = 0;  // 0=down, 1=up
    rv = get_link_status(fd, &status);
    if (rv < 0) {
        perror("get_link_status() failed");
        return -1;  // failure
    }
    if (proto_opt.log >= 1) {
        printf("link status = %d\n", status);
    }

    link_state_t *link = get_link_state(proto_opt.if_index);
    if (!link) {
        perror("get_link_state() failed");
        return -1;  // failure
    }

    rv = server(fd, link);

    close(fd);

    return rv;
}
