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
    &&  (if_index < (sizeof(user_state) / sizeof(user_state_t)))) {
        return &user_state[if_index];
    }
    return NULL;
}

link_state_t link_state[16];    // link state by if_index

link_state_t *
get_link_state(int if_index)
{
    if ((if_index > 0)
    &&  (if_index < (sizeof(link_state) / sizeof(link_state_t)))) {
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

#include "../include/link_ssm.c"

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

static int
simulate_userspace(user_state_t *user, link_state_t *link)
{
    /*
     * outbound AIT
     */
    if (proto_opt.ait
     && !GET_FLAG(user->user_flags, UF_FULL)
     && !GET_FLAG(link->link_flags, LF_BUSY)) {
        // initiate outbound transfer
        //size_t n = strlen(proto_opt.ait);
#pragma GCC diagnostic ignored "-Wstringop-truncation"
	strncpy((char*)user->outbound, proto_opt.ait, MAX_PAYLOAD);
#pragma GCC diagnostic pop
        SET_FLAG(user->user_flags, UF_FULL);
        LOG_INFO("userspace: outbound AIT (%u octets)\n", MAX_PAYLOAD);
        HEX_DEBUG(user->outbound, MAX_PAYLOAD);
    }
    if (GET_FLAG(user->user_flags, UF_FULL)
     && GET_FLAG(link->link_flags, LF_BUSY)) {
        // acknowlege outbound transfer
        size_t n = strlen(proto_opt.ait);
        if (n >= MAX_PAYLOAD) {
            proto_opt.ait += MAX_PAYLOAD;
        } else {
            proto_opt.ait = NULL;
        }
        CLR_FLAG(user->user_flags, UF_FULL);
    }

    /*
     * inbound AIT
     */
    if (!GET_FLAG(user->user_flags, UF_BUSY)
     && GET_FLAG(link->link_flags, LF_FULL)) {
        // receive inbound transfer
        SET_FLAG(user->user_flags, UF_BUSY);
        LOG_INFO("userspace: inbound AIT (%u octets)\n", MAX_PAYLOAD);
        HEX_DEBUG(link->inbound, MAX_PAYLOAD);
    }
    if (GET_FLAG(user->user_flags, UF_BUSY)
     && !GET_FLAG(link->link_flags, LF_FULL)) {
        // acknowlege inbound transfer
        CLR_FLAG(user->user_flags, UF_BUSY);
    }

    return 1;  // success
//    return 0;  // failure
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

        simulate_userspace(user, link);

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
