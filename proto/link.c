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

#define PERMISSIVE   0  // allow non-protocol frames to pass through
#define LOG_LEVEL    2  // log level (0=none, 1=info, 2=debug, 3=trace)
#define FRAME_LIMIT  7  // halt ping/pong after limited number of frames
#define MAX_PAYLOAD  44 // maxiumum number of AIT data octets
#define TEST_OVERLAP 0  // run server() twice to test overlapping init

enum xdp_action {
    XDP_ABORTED = 0,
    XDP_DROP,
    XDP_PASS,
    XDP_TX,
    XDP_REDIRECT,
};

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
    __u8        outbound[44];   // outbound data buffer
    __u32       user_flags;     // flags controller by user
    __u8        inbound[44];    // inbound data buffer
    __u32       link_flags;     // flags controller by link
    __u8        frame[64];      // transport frame
    protocol_t  i;              // local protocol state
    protocol_t  u;              // remote protocol state
    __u16       len;            // payload length
    __u32       seq;            // sequence number
} link_state_t;

#define LF_ID_A (((__u32)1)<<0) // endpoint role Alice
#define LF_ID_B (((__u32)1)<<1) // endpoint role Bob
#define LF_ENTL (((__u32)1)<<2) // link entangled
#define LF_FULL (((__u32)1)<<3) // outbound AIT full
#define LF_VALD (((__u32)1)<<4) // inbound AIT valid
#define LF_SEND (((__u32)1)<<5) // link sending AIT
#define LF_RECV (((__u32)1)<<6) // link receiving AIT

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
    INT2SMOL(0),                         // state = {i:0, u:0}
    INT2SMOL(0),                         // payload len = 0
};
static octet_t *eth_remote = &proto_init[0 * ETH_ALEN];
static octet_t *eth_local = &proto_init[1 * ETH_ALEN];


/* always print warnings and errors */
#define LOG_WARN(fmt, ...)  LOG_PRINT(0, (fmt), ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  LOG_PRINT(0, (fmt), ##__VA_ARGS__)

#define LOG_PRINT(level, fmt, ...)  ({         \
    if (proto_opt.log >= (level)) {            \
        fprintf(stderr, (fmt), ##__VA_ARGS__); \
    }                                          \
})
#define LOG_INFO(fmt, ...)   LOG_PRINT(1, (fmt), ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)  LOG_PRINT(2, (fmt), ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...)  LOG_PRINT(3, (fmt), ##__VA_ARGS__)

#define MAC_PRINT(level, tag, mac)  ({        \
    if (proto_opt.log >= (level)) {           \
        print_mac_addr(stderr, (tag), (mac)); \
    }                                         \
})
#define MAC_TRACE(tag, mac)  MAC_PRINT(3, (tag), (mac))

#define HEX_DUMP(level, buf, len) ({   \
    if (proto_opt.log >= (level)) {    \
        hexdump(stderr, (buf), (len)); \
    }                                  \
})
#define HEX_INFO(buf, len)   HEX_DUMP(1, (buf), (len))
#define HEX_DEBUG(buf, len)  HEX_DUMP(2, (buf), (len))
#define HEX_TRACE(buf, len)  HEX_DUMP(3, (buf), (len))


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
    DEBUG(dump_sockaddr(stderr, addr, addr_len));

    clock_gettime(CLOCK_REALTIME, ts);

    n = sendto(fd, data, size, 0, addr, addr_len);
    if (n < 0) return n;  // sendto error

    DEBUG(dump_sockaddr(stderr, addr, addr_len));

    LOG_TRACE("%zu.%09ld Message[%d] --> \n",
        (size_t)ts->tv_sec, (long)ts->tv_nsec, n);
    HEX_TRACE(data, size);

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

    DEBUG(dump_sockaddr(stderr, addr, addr_len));

    LOG_TRACE("%zu.%09ld Message[%d] <-- \n",
        (size_t)ts->tv_sec, (long)ts->tv_nsec, n);
    HEX_TRACE(data, (n < 0 ? limit : n));

    return n;
}

static __inline int
cmp_mac_addr(void *dst, void *src)
{
    __u8 *d = dst;
    __u8 *s = src;
    int dir;

    dir = ((int)d[5] - (int)s[5]);
    if (dir) return dir;
    dir = ((int)d[4] - (int)s[4]);
    if (dir) return dir;
    dir = ((int)d[3] - (int)s[3]);
    if (dir) return dir;
    dir = ((int)d[2] - (int)s[2]);
    if (dir) return dir;
    dir = ((int)d[1] - (int)s[1]);
    if (dir) return dir;
    dir = ((int)d[0] - (int)s[0]);
    return dir;
}

static __inline int
mac_is_bcast(void *mac)
{
    __u8 *b = mac;

    return ((b[0] & b[1] & b[2] & b[3] & b[4] & b[5]) == 0xFF);
}

#define GET_FLAG(lval,rval) !!((lval) & (rval))
#define SET_FLAG(lval,rval) (lval) |= (rval)
#define CLR_FLAG(lval,rval) (lval) &= ~(rval)

#define PROTO(i, u) (0200 | ((i) & 07) << 3 | ((u) & 07))
#define PARSE_PROTO(i, u, b) ({ \
    i = ((b) & 0070) >> 3;      \
    u = ((b) & 0007);           \
})

static __inline int
check_src_mac(__u8 *src)
{
    if (cmp_mac_addr(eth_remote, src) != 0) {
        MAC_TRACE("expect = ", eth_remote);
        MAC_TRACE("actual = ", src);
        LOG_ERROR("Unexpected peer address!\n");
        return -1;  // failure
    }
    return 0;  // success
}

static int
outbound_AIT(link_state_t *link)
{
/*
    if there is not AIT in progress already
    and there is outbound data to send
    copy the data into the link buffer
    and set AIT-in-progress flags
*/
    if (proto_opt.ait) {
        size_t n = strlen(proto_opt.ait);
        link->len = (n > MAX_PAYLOAD) ? MAX_PAYLOAD : n;
        memcpy(link->frame + ETH_HLEN + 2, proto_opt.ait, link->len);
        LOG_INFO("outbound_AIT (%u of %u)\n", link->len, n);
        HEX_INFO(proto_opt.ait, link->len);
        return 1;  // send AIT
    }
    return 0;  // no AIT
}

static int
inbound_AIT(link_state_t *link, __u8 *payload)
{
/*
    if there is not AIT in progress already
    copy the data into the link buffer
    and set AIT-in-progress flags
*/
    LOG_INFO("inbound_AIT (%u octets)\n", link->len);
    if (link->len > 0) {
        memcpy(link->frame + ETH_HLEN + 2, payload, link->len);
        return 1;  // success
    }
    link->len = 0;
    return 0;  // failure
}

static int
release_AIT(link_state_t *link)
{
/*
    if the client has room to accept the AIT
    copy the data from the link buffer
    and clear AIT-in-progress flags
*/
    LOG_INFO("release_AIT (%u octets)\n", link->len);
    HEX_INFO(link->frame + ETH_HLEN + 2, link->len);
    return 1;  // AIT released
//    return 0;  // reject AIT
}

static int
clear_AIT(link_state_t *link)
{
/*
    acknowlege successful AIT
    and clear AIT-in-progress flags
*/
    if (proto_opt.ait) {
        size_t n = strlen(proto_opt.ait);
        if (link->len < n) {
            proto_opt.ait += link->len;
        } else {
            proto_opt.ait = NULL;
        }
        LOG_INFO("clear_AIT (%u of %u)\n", link->len, n);
    }
    link->len = 0;
    return 1;  // success
//    return 0;  // failure
}

static int
on_frame_recv(__u8 *data, __u8 *end, link_state_t *link)
{
    protocol_t i;
    protocol_t u;

    // parse protocol state
    __u8 proto = data[ETH_HLEN + 0];
    if ((proto & 0300) != 0200) {
        LOG_WARN("Bad format (proto=0%o)\n", proto);
        return XDP_DROP;  // bad format
    }
    PARSE_PROTO(i, u, proto);
    LOG_DEBUG("  (%u,%u) <--\n", i, u);
    link->i = u;

    // parse payload length
    __u8 len = SMOL2INT(data[ETH_HLEN + 1]);
    if (len > 44) {
        LOG_WARN("Bad format (len=%u > 44)\n", len);
        return XDP_DROP;  // bad format
    }
    __u8 *dst = data;
    __u8 *src = data + ETH_ALEN;
    if (proto_opt.log >= 3) {
        MAC_TRACE("dst = ", dst);
        MAC_TRACE("src = ", src);
        LOG_TRACE("len = %d\n", len);
    }
    link->len = 0;

    // protocol state machine
    switch (proto) {
        case PROTO(Init, Init) : {
            if (len != 0) {
                LOG_WARN("Unexpected payload (len=%d)\n", len);
                return XDP_DROP;  // unexpected payload
            }
            link->seq = 0;
            link->link_flags = 0;
            link->user_flags = 0;
            if (mac_is_bcast(dst)) {
                LOG_DEBUG("dst mac is bcast\n");
                link->u = Init;
            } else {
                int dir = cmp_mac_addr(dst, src);
                LOG_TRACE("cmp(dst, src) = %d\n", dir);
                if (dir < 0) {
                    if (GET_FLAG(link->link_flags, LF_ENTL)) {
                        LOG_INFO("Drop overlapped Init!\n");
                        return XDP_DROP;  // drop overlapped init
                    }
                    SET_FLAG(link->link_flags, LF_ENTL | LF_ID_B);
                    LOG_DEBUG("ENTL set on send\n");
                    LOG_INFO("Bob sending initial Ping\n");
                    link->u = Ping;
                } else if (dir > 0) {
                    LOG_INFO("Alice breaking symmetry\n");
                    link->u = Init;  // Alice breaking symmetry
                } else {
                    LOG_ERROR("Identical srs/dst mac\n");
                    return XDP_DROP;  // identical src/dst mac
                }
            }
            memcpy(eth_remote, src, ETH_ALEN);
            if (proto_opt.log >= 1) {
                MAC_TRACE("eth_remote = ", eth_remote);
//                MAC_TRACE("eth_local = ", eth_local);
            }
            memcpy(link->frame, eth_remote, ETH_ALEN);
            break;
        }
        case PROTO(Init, Ping) : {
            if (cmp_mac_addr(dst, src) < 0) {
                LOG_ERROR("Bob received Ping!\n");
                return XDP_DROP;  // wrong role for ping
            }
            if (GET_FLAG(link->link_flags, LF_ENTL)) {
                LOG_INFO("Drop overlapped Ping!\n");
                return XDP_DROP;  // drop overlapped ping
            }
            SET_FLAG(link->link_flags, LF_ENTL | LF_ID_A);
            LOG_DEBUG("ENTL set on recv\n");
            LOG_INFO("Alice sending initial Pong\n");
            link->u = Pong;
            break;
        }
        case PROTO(Proceed, Ping) : /* FALL-THRU */
        case PROTO(Pong, Ping) : {
            if (check_src_mac(src) < 0) return XDP_DROP;  // failure
            if (!GET_FLAG(link->link_flags, LF_ID_A)) {
                LOG_INFO("Ping is for Alice!\n");
                return XDP_DROP;  // wrong role for ping
            }
            if (outbound_AIT(link)) {
                link->u = Got_AIT;
            } else {
                link->u = Pong;
            }
            break;
        }
        case PROTO(Proceed, Pong) : /* FALL-THRU */
        case PROTO(Ping, Pong) : {
            if (check_src_mac(src) < 0) return XDP_DROP;  // failure
            if (!GET_FLAG(link->link_flags, LF_ID_B)) {
                LOG_INFO("Pong is for Bob!\n");
                return XDP_DROP;  // wrong role for pong
            }
            if (outbound_AIT(link)) {
                link->u = Got_AIT;
            } else {
                link->u = Ping;
            }
            break;
        }
        case PROTO(Ping, Got_AIT) : {
            link->len = len;
            if (inbound_AIT(link, data + ETH_HLEN + 2)) {
                link->u = Ack_AIT;
            } else {
                link->u = Ping;
            }
            break;
        }
        case PROTO(Got_AIT, Ping) : {  // reverse
            link->u = Pong;  // give the other end a chance to send
            break;
        }
        case PROTO(Pong, Got_AIT) : {
            link->len = len;
            if (inbound_AIT(link, data + ETH_HLEN + 2)) {
                link->u = Ack_AIT;
            } else {
                link->u = Pong;
            }
            break;
        }
        case PROTO(Got_AIT, Pong) : {  // reverse
            link->u = Ping;  // give the other end a chance to send
            break;
        }
        case PROTO(Got_AIT, Ack_AIT) : {
            link->len = len;
            link->u = Ack_Ack;
            break;
        }
        case PROTO(Ack_AIT, Got_AIT) : {  // reverse
            if (GET_FLAG(link->link_flags, LF_ID_B)) {
                link->u = Ping;
            } else {
                link->u = Pong;
            }
            break;
        }
        case PROTO(Ack_AIT, Ack_Ack) : {
            link->len = len;
            if (release_AIT(link)) {
                link->u = Proceed;
            } else {
                link->u = Ack_AIT;  // reverse
            }
            break;
        }
        case PROTO(Ack_Ack, Ack_AIT) : {  // reverse
            link->u = Got_AIT;
            break;
        }
        case PROTO(Ack_Ack, Proceed) : {
            link->len = len;
            clear_AIT(link);
            if (GET_FLAG(link->link_flags, LF_ID_B)) {
                link->u = Ping;
            } else {
                link->u = Pong;
            }
            break;
        }
        default: {
            LOG_ERROR("Bad state (%u,%u)\n", i, u);
            return XDP_DROP;  // bad state
        }
    }

    // construct reply frame
    link->seq += 1;
    link->frame[ETH_HLEN + 0] = PROTO(link->i, link->u);
    link->frame[ETH_HLEN + 1] = INT2SMOL(link->len);
    size_t n = ETH_HLEN + 2 + link->len; 
    memset(link->frame + n, null, sizeof(link->frame) - n);
    LOG_DEBUG("  (%u,%u) #%u -->\n", link->i, link->u, link->seq);

    return XDP_TX;  // send updated frame out on same interface
}

int
xdp_filter(void *data, void *end, link_state_t *link)
{
    size_t data_len = end - data;

    if (data + ETH_ZLEN > end) {
        LOG_ERROR("frame too small. expect=%zu, actual=%zu\n",
            (size_t)ETH_ZLEN, data_len);
        return XDP_DROP;  // frame too small
    }
    struct ethhdr *eth = data;
    __u16 eth_proto = ntohs(eth->h_proto);
    if (eth_proto != ETH_P_DALE) {
#if PERMISSIVE
        return XDP_PASS;  // pass on to default network stack
#else
        LOG_WARN("wrong protocol. expect=0x%x, actual=0x%x\n",
            ETH_P_DALE, eth_proto);
        return XDP_DROP;  // wrong protocol
#endif
    }

    int rc = on_frame_recv(data, end, link);
    LOG_TRACE("recv: proto=0x%x len=%zu rc=%d\n", eth_proto, data_len, rc);

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
        LOG_DEBUG("  (%u,%u) #%u -->\n", link->i, link->u, link->seq);
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

#if FRAME_LIMIT
        if (link->seq > FRAME_LIMIT) {
            LOG_WARN("frame %u exceeded limit\n", link->seq);
            return XDP_ABORTED;  // halt ping/pong!
        }
#endif
    }
}

#ifndef TEST_MAIN
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
    LOG_TRACE("CLOCK_REALTIME resolution %zu.%09ld\n",
        (size_t)ts.tv_sec, (long)ts.tv_nsec);

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
    LOG_INFO("link status = %d\n", status);

    link_state_t *link = get_link_state(proto_opt.if_index);
    if (!link) {
        perror("get_link_state() failed");
        return -1;  // failure
    }

    rv = server(fd, link);
#if TEST_OVERLAP
    LOG_INFO("server: rv = %d\n", rv);
    rv = server(fd, link);
#endif

    close(fd);

    return rv;
}
#endif /* !TEST_MAIN */

#ifdef TEST_MAIN
#include <assert.h>

void
link_test()
{
    octet_t bcast_mac[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    octet_t alice_mac[ETH_ALEN] = { 0xdc, 0xa6, 0x32, 0x67, 0x7e, 0xa7 };
    octet_t carol_mac[ETH_ALEN] = { 0xb8, 0x27, 0xeb, 0xf3, 0x5a, 0xd2 };

    memcpy(eth_local, alice_mac, ETH_ALEN);
    memcpy(eth_remote, carol_mac, ETH_ALEN);
    print_mac_addr(stdout, "bcast = ", bcast_mac);
    print_mac_addr(stdout, "alice = ", eth_local);
    print_mac_addr(stdout, "carol = ", eth_remote);

    assert(cmp_mac_addr(bcast_mac, bcast_mac) == 0);
    assert(cmp_mac_addr(bcast_mac, alice_mac) > 0);
    assert(cmp_mac_addr(alice_mac, bcast_mac) < 0);
    assert(cmp_mac_addr(bcast_mac, carol_mac) > 0);
    assert(cmp_mac_addr(carol_mac, bcast_mac) < 0);

    assert(cmp_mac_addr(alice_mac, alice_mac) == 0);
    assert(cmp_mac_addr(carol_mac, carol_mac) == 0);
    assert(cmp_mac_addr(alice_mac, carol_mac) < 0);
    assert(cmp_mac_addr(carol_mac, alice_mac) > 0);

    assert(mac_is_bcast(bcast_mac) == 1);
    assert(mac_is_bcast(alice_mac) == 0);
    assert(mac_is_bcast(carol_mac) == 0);
}

int
main()
{
    link_test();
    return 0;  // success!
}
#endif /* TEST_MAIN */
