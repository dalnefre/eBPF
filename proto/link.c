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

#include "../include/link.h"

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

//static octet_t proto_buf[256];  // message-transfer buffer
static octet_t proto_buf[64];  // message-transfer buffer
//static octet_t proto_buf[ETH_ZLEN];  // message-transfer buffer
//static octet_t proto_buf[ETH_MIN_MTU];  // message-transfer buffer

static octet_t proto_init[64] = {
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
#define LOG_TEMP(fmt, ...)  LOG_PRINT(0, (fmt), ##__VA_ARGS__)
//#define LOG_TEMP(fmt, ...)  /* REMOVED */

#define LOG_PRINT(level, fmt, ...)  ({         \
    if (proto_opt.log >= (level)) {            \
        fprintf(stderr, (fmt), ##__VA_ARGS__); \
    }                                          \
})
#define LOG_INFO(fmt, ...)   LOG_PRINT(1, (fmt), ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)  LOG_PRINT(2, (fmt), ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...)  LOG_PRINT(3, (fmt), ##__VA_ARGS__)
#define LOG_EXTRA(fmt, ...)  LOG_PRINT(4, (fmt), ##__VA_ARGS__)

#define MAC_PRINT(level, tag, mac)  ({        \
    if (proto_opt.log >= (level)) {           \
        print_mac_addr(stderr, (tag), (mac)); \
    }                                         \
})
#define MAC_TRACE(tag, mac)  MAC_PRINT(3, (tag), (mac))
#define MAC_EXTRA(tag, mac)  MAC_PRINT(4, (tag), (mac))

#define HEX_DUMP(level, buf, len) ({   \
    if (proto_opt.log >= (level)) {    \
        hexdump(stderr, (buf), (len)); \
    }                                  \
})
#define HEX_INFO(buf, len)   HEX_DUMP(1, (buf), (len))
#define HEX_DEBUG(buf, len)  HEX_DUMP(2, (buf), (len))
#define HEX_TRACE(buf, len)  HEX_DUMP(3, (buf), (len))
#define HEX_EXTRA(buf, len)  HEX_DUMP(4, (buf), (len))


user_state_t user_state[16];    // user state by if_index

user_state_t *
get_user_state(int if_index)
{
    if ((if_index > 0)
    ||  (if_index < (sizeof(user_state) / sizeof(user_state_t)))) {
        return &user_state[if_index];
    }
    return NULL;
}

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

    LOG_EXTRA("%zu.%09ld Message[%d] --> \n",
        (size_t)ts->tv_sec, (long)ts->tv_nsec, n);
    HEX_EXTRA(data, size);

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

    LOG_EXTRA("%zu.%09ld Message[%d] <-- \n",
        (size_t)ts->tv_sec, (long)ts->tv_nsec, n);
    HEX_EXTRA(data, (n < 0 ? limit : n));

    return n;
}

/*** <sync from="../XDP/link_kern.c" to="../proto/link.c"> ***/

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

#define copy_payload(dst,src)  memcpy((dst), (src), MAX_PAYLOAD)
#define clear_payload(dst)     memset((dst), null, MAX_PAYLOAD)

static int
outbound_AIT(user_state_t *user, link_state_t *link)
{
/*
    if there is not AIT in progress already
    and there is outbound data to send
    copy the data into the link buffer
    and set AIT-in-progress flags
*/
    if ((GET_FLAG(user->user_flags, UF_FULL)
      && !GET_FLAG(link->link_flags, LF_BUSY))
    ||  GET_FLAG(link->link_flags, LF_SEND)) {
//    LOG_TEMP("outbound_AIT: user_flags=0x%x link_flags=0x%x\n",
//        user->user_flags, link->link_flags);
        if (GET_FLAG(link->link_flags, LF_BUSY)) {
            LOG_TEMP("outbound_AIT: resending (LF_BUSY)\n");
        } else {
            LOG_TEMP("outbound_AIT: setting LF_SEND + LF_BUSY\n");
            SET_FLAG(link->link_flags, LF_SEND);
            SET_FLAG(link->link_flags, LF_BUSY);
        }
        copy_payload(link->frame + ETH_HLEN + 2, user->outbound);
        link->len = MAX_PAYLOAD;
        LOG_INFO("outbound_AIT (%u octets)\n", link->len);
        HEX_INFO(user->outbound, link->len);
        return 1;  // send AIT
    }
    return 0;  // no AIT
}

static int
inbound_AIT(user_state_t *user, link_state_t *link, __u8 *payload)
{
/*
    if there is not AIT in progress already
    copy the data into the link buffer
    and set AIT-in-progress flags
*/
    LOG_INFO("inbound_AIT (%u octets)\n", link->len);
    if (!GET_FLAG(link->link_flags, LF_RECV)
    &&  (link->len > 0)) {
        LOG_TEMP("inbound_AIT: setting LF_RECV\n");
        SET_FLAG(link->link_flags, LF_RECV);
        copy_payload(link->frame + ETH_HLEN + 2, payload);
        return 1;  // success
    }
    link->len = 0;
    return 0;  // failure
}

static int
release_AIT(user_state_t *user, link_state_t *link)
{
/*
    if the client has room to accept the AIT
    copy the data from the link buffer
    and clear AIT-in-progress flags
*/
    if (GET_FLAG(link->link_flags, LF_RECV)
    &&  !GET_FLAG(user->user_flags, UF_BUSY)
    &&  !GET_FLAG(link->link_flags, LF_FULL)) {
        LOG_TEMP("release_AIT: setting LF_FULL\n");
        copy_payload(link->inbound, link->frame + ETH_HLEN + 2);
        SET_FLAG(link->link_flags, LF_FULL);
        LOG_INFO("release_AIT (%u octets)\n", link->len);
        HEX_INFO(link->inbound, link->len);
        return 1;  // AIT released
    }
    return 0;  // reject AIT
}

static int
clear_AIT(user_state_t *user, link_state_t *link)
{
/*
    acknowlege successful AIT
    and clear AIT-in-progress flags
*/
    if (GET_FLAG(link->link_flags, LF_SEND)) {
        LOG_TEMP("clear_AIT: setting !LF_SEND\n");
        CLR_FLAG(link->link_flags, LF_SEND);
        if (GET_FLAG(link->link_flags, LF_BUSY)
        &&  !GET_FLAG(user->user_flags, UF_FULL)) {
            LOG_TEMP("clear_AIT: setting !LF_BUSY\n");
            CLR_FLAG(link->link_flags, LF_BUSY);
            LOG_INFO("clear_AIT (%u octets)\n", link->len);
        } else {
            LOG_WARN("clear_AIT: outbound VALID still set!\n");
        }
    } else {
        LOG_WARN("clear_AIT: outbound SEND not set!\n");
    }
    link->len = 0;
    return 1;  // success
//    return 0;  // failure
}

static int
on_frame_recv(__u8 *data, __u8 *end, user_state_t *user, link_state_t *link)
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
    if ((i < Got_AIT) && (u < Got_AIT)) {
        LOG_TRACE("  (%u,%u) <--\n", i, u);
    } else {
        LOG_DEBUG("  (%u,%u) <--\n", i, u);
    }
    link->i = u;

    // parse payload length
    __u8 len = SMOL2INT(data[ETH_HLEN + 1]);
    if (len > MAX_PAYLOAD) {
        LOG_WARN("Bad format (len=%u > %u)\n", len, MAX_PAYLOAD);
        return XDP_DROP;  // bad format
    }
    __u8 *dst = data;
    __u8 *src = data + ETH_ALEN;
    MAC_TRACE("dst = ", dst);
    MAC_TRACE("src = ", src);
    LOG_TRACE("len = %d\n", len);
    link->len = 0;

    // update async flags
    if (!GET_FLAG(link->link_flags, LF_SEND)
    &&  GET_FLAG(link->link_flags, LF_BUSY)
    &&  !GET_FLAG(user->user_flags, UF_FULL)) {
        LOG_TEMP("on_frame_recv: setting !LF_BUSY\n");
        CLR_FLAG(link->link_flags, LF_BUSY);
        LOG_TRACE("outbound BUSY cleared.\n");
    }
    if (GET_FLAG(user->user_flags, UF_BUSY)
    &&  GET_FLAG(link->link_flags, LF_RECV)
    &&  GET_FLAG(link->link_flags, LF_FULL)) {
        LOG_TEMP("on_frame_recv: setting !LF_FULL + !LF_RECV\n");
        CLR_FLAG(link->link_flags, LF_FULL);
        CLR_FLAG(link->link_flags, LF_RECV);
        LOG_TRACE("inbound FULL + RECV cleared.\n");
    }

    // protocol state machine
    switch (proto) {
        case PROTO(Init, Init) : {
            if (len != 0) {
                LOG_WARN("Unexpected payload (len=%d)\n", len);
                return XDP_DROP;  // unexpected payload
            }
            link->seq = 0;
            LOG_TEMP("on_frame_recv: clearing LF_* + UF_*\n");
            link->link_flags = 0;
//            user->user_flags = 0;  // FIXME: can't write to user_state?
            if (mac_is_bcast(dst)) {
                LOG_INFO("Init: dst mac is bcast\n");
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
            MAC_TRACE("eth_remote = ", src);
            memcpy(link->frame, src, ETH_ALEN);
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
            if (cmp_mac_addr(link->frame, src) != 0) {
                MAC_TRACE("expect = ", link->frame);
                MAC_TRACE("actual = ", src);
                LOG_ERROR("Unexpected peer address!\n");
                return XDP_DROP;  // wrong peer mac
            }
            if (!GET_FLAG(link->link_flags, LF_ID_A)) {
                LOG_INFO("Ping is for Alice!\n");
                return XDP_DROP;  // wrong role for ping
            }
            if (outbound_AIT(user, link)) {
                link->u = Got_AIT;
            } else {
                link->u = Pong;
            }
            break;
        }
        case PROTO(Proceed, Pong) : /* FALL-THRU */
        case PROTO(Ping, Pong) : {
            if (cmp_mac_addr(link->frame, src) != 0) {
                MAC_TRACE("expect = ", link->frame);
                MAC_TRACE("actual = ", src);
                LOG_ERROR("Unexpected peer address!\n");
                return XDP_DROP;  // wrong peer mac
            }
            if (!GET_FLAG(link->link_flags, LF_ID_B)) {
                LOG_INFO("Pong is for Bob!\n");
                return XDP_DROP;  // wrong role for pong
            }
            if (outbound_AIT(user, link)) {
                link->u = Got_AIT;
            } else {
                link->u = Ping;
            }
            break;
        }
        case PROTO(Ping, Got_AIT) : {
            link->len = len;
            LOG_TEMP("on_frame_recv: (Ping, Got_AIT) len=%d\n", len);
            if (inbound_AIT(user, link, data + ETH_HLEN + 2)) {
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
            LOG_TEMP("on_frame_recv: (Pong, Got_AIT) len=%d\n", len);
            if (inbound_AIT(user, link, data + ETH_HLEN + 2)) {
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
            LOG_TEMP("on_frame_recv: (Got_AIT, Ack_AIT) len=%d\n", len);
            link->u = Ack_Ack;
            break;
        }
        case PROTO(Ack_AIT, Got_AIT) : {  // reverse
            LOG_TEMP("on_frame_recv: clearing LF_RECV (rev Got_AIT)\n");
            CLR_FLAG(link->link_flags, LF_RECV);
            // FIXME: consider sending AIT, if we have data to send
            if (GET_FLAG(link->link_flags, LF_ID_B)) {
                link->u = Ping;
            } else {
                link->u = Pong;
            }
            break;
        }
        case PROTO(Ack_AIT, Ack_Ack) : {
            link->len = len;
            LOG_TEMP("on_frame_recv: (Ack_AIT, Ack_Ack) len=%d\n", len);
            if (release_AIT(user, link)) {
                link->u = Proceed;
            } else {
                LOG_TEMP("on_frame_recv: release failed, reversing!\n");
                link->u = Ack_AIT;  // reverse
            }
            break;
        }
        case PROTO(Ack_Ack, Ack_AIT) : {  // reverse
            LOG_TEMP("on_frame_recv: reverse Ack_AIT\n");
            link->u = Got_AIT;
            break;
        }
        case PROTO(Ack_Ack, Proceed) : {
            link->len = len;
            LOG_TEMP("on_frame_recv: (Ack_Ack, Proceed) len=%d\n", len);
            clear_AIT(user, link);
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
    if (link->len == 0) {
        clear_payload(link->frame + ETH_HLEN + 2);
    }
    if ((link->i < Got_AIT) && (link->u < Got_AIT)) {
        LOG_TRACE("  (%u,%u) #%u -->\n", link->i, link->u, link->seq);
    } else {
        LOG_DEBUG("  (%u,%u) #%u -->\n", link->i, link->u, link->seq);
    }

    return XDP_TX;  // send updated frame out on same interface
}

/*** </sync> ***/

int
xdp_filter(void *data, void *end, user_state_t *user, link_state_t *link)
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

    int rc = on_frame_recv(data, end, user, link);
    LOG_EXTRA("recv: proto=0x%x len=%zu rc=%d\n", eth_proto, data_len, rc);

    if (rc == XDP_TX) {
        memcpy(data, link->frame, ETH_ZLEN);  // copy frame to i/o buffer
    }

    return rc;
}

int
send_init(int fd, user_state_t *user, link_state_t *link)
{
    struct timespec ts;
    int n;

    link->i = Init;
    link->u = Init;
    link->seq = 0;
    link->link_flags = 0;
    user->user_flags = 0;
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
server(int fd, user_state_t *user, link_state_t *link)
{
    struct timespec ts;
    int rc, n;

    if (send_init(fd, user, link) < 0) return -1;  // failure

    for (;;) {

        n = recv_message(fd, proto_buf, sizeof(proto_buf), &ts);
        if (n <= 0) {
            perror("recv_message() failed");
            return -1;  // failure
        }

        rc = xdp_filter(proto_buf, proto_buf + n, user, link);
        if (rc == XDP_ABORTED) {
            perror("XDP_ABORTED");
            return -1;  // failure
        }

        if (rc == XDP_TX) {
            n = send_message(fd, proto_buf, ETH_ZLEN, &ts);
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

#if 1
    // demonstrate log levels
    LOG_WARN("LOG_WARN: log=%d\n", proto_opt.log);
    LOG_INFO("LOG_INFO: log=%d\n", proto_opt.log);
    LOG_DEBUG("LOG_DEBUG: log=%d\n", proto_opt.log);
    LOG_TRACE("LOG_TRACE: log=%d\n", proto_opt.log);
    LOG_EXTRA("LOG_EXTRA: log=%d\n", proto_opt.log);
#endif

    // determine real-time resolution
    struct timespec ts;
    if (clock_getres(CLOCK_REALTIME, &ts) < 0) {
        perror("clock_getres() failed");
        return -1;
    }
    LOG_EXTRA("CLOCK_REALTIME resolution %zu.%09ld\n",
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
        print_mac_addr(stdout, "eth_remote = ", eth_remote);
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

    user_state_t *user = get_user_state(proto_opt.if_index);
    if (!user) {
        perror("get_user_state() failed");
        return -1;  // failure
    }

    link_state_t *link = get_link_state(proto_opt.if_index);
    if (!link) {
        perror("get_link_state() failed");
        return -1;  // failure
    }

    rv = server(fd, user, link);
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
