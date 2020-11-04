/*
 * ait.c -- Atomic Information Transfer protocol
 */
#include "proto.h"
#include "util.h"
#include "code.h"
#include "json.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#define DEBUG(x)   /**/

#define SHARED_COUNT 0  // message counter is shared (or local)
#define ZERO_COPY    1  // apply in-place edits to packet buffer
#define LOG_RESULT   0  // log result code for all protocol packets
#define LOG_PROTO    1  // log each protocol messages exchange
#define LOG_AIT      1  // log each AIT sent/recv
#define DUMP_PACKETS 0  // hexdump raw packets sent/received
#define PACKET_LIMIT 13 // halt ping/pong after limited number of packets

//static BYTE proto_buf[256];  // message-transfer buffer
static BYTE proto_buf[64];  // message-transfer buffer
//static BYTE proto_buf[ETH_ZLEN];  // message-transfer buffer
//static BYTE proto_buf[ETH_MIN_MTU];  // message-transfer buffer

static BYTE proto_init[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // dst_mac = broadcast
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // src_mac = eth_local
    0xda, 0x1e,                          // protocol ethertype
    array, INT2SMOL(6),                  // array (size=6)
    INT2SMOL(0),                         // state = 0
    INT2SMOL(0),                         // other = 0
    p_int_0, n_2, 0x00, 0x00,            // count = 0 (+INT, pad=0)
    null, null,                          // neutral fill...
};
static BYTE *eth_remote = &proto_init[0 * ETH_ALEN];
static BYTE *eth_local = &proto_init[1 * ETH_ALEN];


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

#if DUMP_PACKETS
    fprintf(stdout, "%zu.%09ld Message[%d] --> \n",
        (size_t)ts->tv_sec, (long)ts->tv_nsec, n);
    hexdump(stdout, data, size);
#endif

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

#if DUMP_PACKETS
    fprintf(stdout, "%zu.%09ld Message[%d] <-- \n",
        (size_t)ts->tv_sec, (long)ts->tv_nsec, n);
    hexdump(stdout, data, (n < 0 ? limit : n));
#endif

    return n;
}

#if ZERO_COPY

enum xdp_action {
    XDP_ABORTED = 0,
    XDP_DROP,
    XDP_PASS,
    XDP_TX,
    XDP_REDIRECT,
};

static __inline void
swap_mac_addrs(void *data)
{
    struct ethhdr *hdr = data;

    __builtin_memcpy(eth_remote, hdr->h_source, ETH_ALEN);
    __builtin_memcpy(hdr->h_source, hdr->h_dest, ETH_ALEN);
    __builtin_memcpy(hdr->h_dest, eth_remote, ETH_ALEN);
}

static __inline void
copy_ait(void *dst, void *src)
{
    __builtin_memcpy(dst, src, 8);
}

static __inline __u64 *
acquire_ait()
{
    return (__u64 *)proto_opt.ait;
}

static __inline int
clear_outbound()
{
    if (proto_opt.ait) {
        size_t z = sizeof(__u64);
        if (strlen(proto_opt.ait) < z) {
            proto_opt.ait = NULL;
        } else {
            proto_opt.ait += z;
        }
    }
    return 0;  // no-op (success)
}

static __inline int
release_ait(__u64 ait)
{
    static int once = -1;  // FIXME: fail, but only once...

    int rv = once;
    once = 0;
    return rv;
}

static __inline int
update_seq_num(int count)
{
#if SHARED_COUNT
    return ++count;
#else
    static __u16 seq_num = 0;

    return ++seq_num;
#endif
}

#define MESSAGE_OFS   (ETH_HLEN + 0)            // 14
#define MSG_LEN_OFS   (MESSAGE_OFS + 1)         // 15
#define MSG_DATA_OFS  (MESSAGE_OFS + 2)         // 16
#define STATE_OFS     (MSG_DATA_OFS + 0)        // 16
#define OTHER_OFS     (MSG_DATA_OFS + 1)        // 17
#define COUNT_OFS     (MSG_DATA_OFS + 2)        // 18
#define COUNT_LEN_OFS (COUNT_OFS + 1)           // 19
#define COUNT_LSB_OFS (COUNT_OFS + 2)           // 20
#define COUNT_MSB_OFS (COUNT_OFS + 3)           // 21
#define BLOB_OFS      (COUNT_OFS + 4)           // 22
#define BLOB_LEN_OFS  (BLOB_OFS + 1)            // 23
#define BLOB_AIT_OFS  (BLOB_OFS + 2)            // 24
#define AIT_SIZE      (8)                       // 8
#define AIT_I_OFS     (BLOB_AIT_OFS + 0)        // 24
#define AIT_U_OFS     (BLOB_AIT_OFS + AIT_SIZE) // 32
#define MSG_END_OFS   (AIT_U_OFS + AIT_SIZE)    // 40

static int
handle_message(__u8 *data, __u8 *end)
{
    __u8 b;
    __s16 n;
    __u64 i = -1;
    __u64 u = -1;

    if (data + MSG_END_OFS > end) return XDP_DROP;  // message too small
    if (data[MESSAGE_OFS] != array) return XDP_DROP;  // require array
    if (data[COUNT_OFS] != p_int_0) return XDP_DROP;  // require +INT (pad=0)
    if (data[COUNT_LEN_OFS] != n_2) return XDP_DROP;  // require size=2
    n = (data[COUNT_MSB_OFS] << 8) | data[COUNT_LSB_OFS];
    b = data[OTHER_OFS];
#if LOG_PROTO
    printf("  %d,%d #%d <--\n", SMOL2INT(data[STATE_OFS]), SMOL2INT(b), n);
#endif
    if (data[MSG_LEN_OFS] == n_24) {  // ait len = 6 + 18
        // message carries AIT
        if (data[BLOB_OFS] != octets) return XDP_DROP;  // require octets
        if (data[BLOB_LEN_OFS] != n_16) return XDP_DROP;  // require size=16
        copy_ait(&u, data + AIT_I_OFS);
        switch (b) {
            case n_3: {  // got ait
                if (b < data[STATE_OFS]) {  // reverse
                    data[OTHER_OFS] = n_2;
                    data[MSG_LEN_OFS] = n_6;
                    data[BLOB_OFS] = null;
                    data[BLOB_LEN_OFS] = null;
                } else {
                    data[OTHER_OFS] = n_4;
                }
                break;
            }
            case n_4: {  // ack ait
                if (b < data[STATE_OFS]) {  // reverse
                    data[OTHER_OFS] = n_3;
                } else {
                    data[OTHER_OFS] = n_5;
                }
                copy_ait(&i, data + AIT_U_OFS);
                break;
            }
            case n_5: {  // ack ack
                if (release_ait(u) < 0) {  // release failed
                    data[OTHER_OFS] = n_4;  // reverse
                } else {
                    data[OTHER_OFS] = n_6;
#if LOG_AIT
                    printf("RCVD: 0x%llx\n", __builtin_bswap64(u));
#endif
                }
                break;
            }
            case n_6: {  // complete
                data[OTHER_OFS] = n_1;
                data[MSG_LEN_OFS] = n_6;
                data[BLOB_OFS] = null;
                data[BLOB_LEN_OFS] = null;
                if (clear_outbound() < 0) return XDP_DROP;  // clear failed
#if LOG_AIT
                copy_ait(&i, data + AIT_U_OFS);
                printf("SENT: 0x%llx\n", __builtin_bswap64(i));
#endif
                i = u = -1;  // clear ait
                break;
            }
            default: return XDP_DROP;  // bad state
        }
    } else if (data[MSG_LEN_OFS] != n_6) {  // liveness len = 6
        return XDP_DROP;  // bad message length
    } else {
        // liveness message
        switch (b) {
            case n_0: {  // init
                data[OTHER_OFS] = n_1;
                break;
            }
            case n_1: {  // ping
                data[OTHER_OFS] = n_2;
                __u64 *p = acquire_ait();
                if (p && (*p != -1)) {  // outbound ait?
                    i = *p;
                    data[OTHER_OFS] = n_3;
                    data[MSG_LEN_OFS] = n_24;
                    data[BLOB_OFS] = octets;
                    data[BLOB_LEN_OFS] = n_16;
                }
                break;
            }
            case n_2: {  // pong
                data[OTHER_OFS] = n_1;
                if (b > data[STATE_OFS]) {  // forward transition
                    __u64 *p = acquire_ait();
                    if (p && (*p != -1)) {  // outbound ait?
                        i = *p;
                        data[OTHER_OFS] = n_3;
                        data[MSG_LEN_OFS] = n_24;
                        data[BLOB_OFS] = octets;
                        data[BLOB_LEN_OFS] = n_16;
                    }
                }
                break;
            }
            default: return XDP_DROP;  // bad state
        }
    }
    // common processing
    n = update_seq_num(n);
#if PACKET_LIMIT
    if (n > PACKET_LIMIT) return XDP_ABORTED;  // FIXME: halt ping/pong!
#endif
    swap_mac_addrs(data);
    copy_ait(data + AIT_I_OFS, &i);
    copy_ait(data + AIT_U_OFS, &u);
    data[STATE_OFS] = b;  // processing state
    data[COUNT_LSB_OFS] = n;
    data[COUNT_MSB_OFS] = n >> 8;
#if LOG_PROTO
    printf("  %d,%d #%d -->\n", SMOL2INT(b), SMOL2INT(data[OTHER_OFS]), n);
#endif
    return XDP_TX;  // send updated frame out on same interface
}

int
xdp_filter(void *data, void *end)
{
#if LOG_RESULT
    size_t data_len = end - data;
#endif

    if (data + ETH_ZLEN > end) {
        return XDP_DROP;  // frame too small
    }
    struct ethhdr *eth = data;
    __u16 eth_proto = ntohs(eth->h_proto);
    if (eth_proto != ETH_P_DALE) {
        return XDP_DROP;  // wrong protocol
    }

    int rc = handle_message(data, end);
#if LOG_RESULT
    printf("proto=0x%x len=%zu rc=%d\n", eth_proto, data_len, rc);
#endif

    return rc;
}

int
server(int fd)
{
    struct timespec ts;
    int rc, n;

    memset(proto_buf, null, sizeof(proto_buf)); 
    memcpy(proto_buf, proto_init, sizeof(proto_init));

    n = send_message(fd, proto_buf, ETH_ZLEN, &ts);
    if (n <= 0) {
        perror("send_message() failed");
        return -1;  // failure
    }

    for (;;) {

        n = recv_message(fd, proto_buf, sizeof(proto_buf), &ts);
        if (n <= 0) {
            perror("recv_message() failed");
            return -1;  // failure
        }

        rc = xdp_filter(proto_buf, proto_buf + n);

        if (rc == XDP_ABORTED) return -1;  // failure

        if (rc == XDP_TX) {
            n = send_message(fd, proto_buf, ETH_ZLEN, &ts);
            if (n <= 0) {
                perror("send_message() failed");
                return -1;  // failure
            }
        }

    }
}

#else /* !ZERO_COPY */

typedef struct ait_msg {
    struct timespec ts;     // timestamp
    int         state;      // self state
    int         other;      // other state
    int         count;      // message count
    struct {
        int64_t     i;      // outbound ait
        int64_t     u;      // inbound ait
    } ait;
} ait_msg_t;

static int
next_state(int state)
{
    switch (state) {
    case 0:  return 1;
    case 1:  return 2;
    case 2:  return 1;
    case 3:  return 4;
    case 4:  return 5;
    case 5:  return 6;
    case 6:  return 1;
    default: return 0;
    }
}

#if 0
static int
prev_state(int state)
{
    switch (state) {
    case 0:  return 0;
    case 1:  return 2;
    case 2:  return 1;
    case 3:  return 2;
    case 4:  return 3;
    case 5:  return 4;
    case 6:  return 5;
    default: return 0;
    }
}
#endif

size_t
create_message(void *data, size_t size, ait_msg_t *msg)
{
    size_t offset = 0;
    ptrdiff_t n;

    // fill Ethernet frame
    if (size < ETH_ZLEN) return 0;  // buffer too small
    memset(data, null, ETH_ZLEN); 

    // add Ethernet header for AF_PACKET SOCK_RAW
    if (size < ETH_HLEN) return 0;  // buffer too small
    struct ethhdr *hdr = data;
    memcpy(hdr->h_dest, eth_remote, ETH_ALEN);
    memcpy(hdr->h_source, eth_local, ETH_ALEN);
    hdr->h_proto = htons(proto_opt.eth_proto);
    offset += ETH_HLEN;

    // payload is a message encoded as an array
    bstr_t meta = {
        .base = data,
        .end = data + offset,
        .limit = data + size,
    };

    if (bstr_open_array(&meta) < 0) return 0;
    if (bstr_put_int(&meta, msg->state) < 0) return 0;
    if (bstr_put_int(&meta, msg->other) < 0) return 0;
    if (bstr_put_int16(&meta, msg->count) < 0) return 0;
    if (msg->other > 2) {
        if (bstr_put_blob(&meta, &msg->ait, sizeof(msg->ait)) < 0) return 0;
    }
    if (bstr_close_array(&meta) < 0) return 0;

    n = (meta.end - meta.start);  // number of bytes written to buffer
    if (n <= 0) return 0;  // error
    offset += n;

    return offset;
}

int
parse_message(void *data, size_t limit, ait_msg_t *msg_in)
{
    size_t offset = 0;
    int rv;

    // parse Ethernet header
    offset += ETH_HLEN;
    if (offset >= limit) return -1;  // out-of-bounds
    struct ethhdr *hdr = data;
    if (hdr->h_proto != htons(proto_opt.eth_proto)) return -1;  // bad protocol
    memcpy(eth_remote, hdr->h_source, ETH_ALEN);  // remember remote MAC

    // parse top-level value
    bstr_t bstr = {
        .base = data,
        .end = data + offset,
        .limit = data + limit,
    };
    json_t json = {
        .bstr = &bstr,
    };
    rv = json_get_value(&json);
    if (rv <= 0) return -1;  // error
    if (json.type != JSON_Array) return -1;  // error

    // parse array elements
    bstr_t part = {
        .end = bstr.cursor,
        .limit = bstr.end,
    };
    json_t item = {
        .bstr = &part,
    };
    part.end = bstr.cursor;
    part.limit = bstr.end;
//    while (part.end < part.limit) {
//        rv = json_get_value(&item);
//        if (rv <= 0) return -1;  // error
//        ...
//    }

    // read "state" from message
    rv = json_get_int64(&item);
    if (rv <= 0) return -1;  // error
    msg_in->state = item.val.num.bits;

    // read "change" from message
    rv = json_get_int64(&item);
    if (rv <= 0) return -1;  // error
    msg_in->other = item.val.num.bits;

    // read "count" from message
    rv = json_get_int64(&item);
    if (rv <= 0) return -1;  // error
    msg_in->count = item.val.num.bits;

    // read "ait" from message
    if (msg_in->other > 2) {
        rv = json_get_value(&item);
        if (rv <= 0) return -1;  // error
        if (item.type != JSON_String) return -1;  // require String
        rv = sizeof(msg_in->ait);
        if (item.count != rv) return -1;  // require size = 16
        memcpy(&msg_in->ait, part.cursor, rv);
    } else {
        msg_in->ait.i = -1;
        msg_in->ait.u = -1;
    }

    offset = bstr.end - bstr.base;  // update final offset
    return offset;
}

int
process_message(ait_msg_t *in, ait_msg_t *out)
{
    // deliver ait
    if (in->other == 5) {
        fputs("AIT rcvd:\n", stdout);
        hexdump(stdout, &in->ait.i, sizeof(in->ait.i));
    }

    // act on message received
    out->state = in->other;
    out->other = next_state(out->state);
    out->ait.u = in->ait.i;
    if (proto_opt.ait) {
        size_t z = sizeof(out->ait.i);
        switch (out->state) {
            case 1:  // FALL-THRU
            case 2:
                strncpy((char *)&out->ait.i, proto_opt.ait, z);
                out->other = 3;
                fputs("AIT send:\n", stdout);
                hexdump(stdout, &out->ait.i, sizeof(out->ait.i));
                break;
            case 6:
                if (strlen(proto_opt.ait) < z) {
                    proto_opt.ait = NULL;
                } else {
                    proto_opt.ait += z;
                }
                break;
        }
    } else {
        out->ait.i = -1;
    }
#if SHARED_COUNT
    out->count = in->count + 1;  // update message counter
#else
    out->count = out->count + 1;  // update message counter
#endif

#if LOG_PROTO
#if DUMP_PACKETS
    printf("  %d,%d #%d -> %d,%d #%d\n",
        in->state, in->other, in->count,
        out->state, out->other, out->count);
#else
    printf("  %zu.%09ld %d,%d #%d -> %d,%d #%d\n",
        (size_t)in->ts.tv_sec, (long)in->ts.tv_nsec,
        in->state, in->other, in->count,
        out->state, out->other, out->count);
#endif
#endif

#if PACKET_LIMIT
    if (out->count > PACKET_LIMIT) return 0;  // FIXME: halt ping/pong!
#endif

    return 1;  // continue...
}

int
server(int fd)
{
    int rv = 1;
    ait_msg_t msg_out = {
        .state = 0,
        .other = 0,
        .count = 0,
        .ait.i = null,
        .ait.u = null,
    };
    ait_msg_t msg_in;

    while (rv > 0) {

        rv = create_message(proto_buf, sizeof(proto_buf), &msg_out);
        if (rv <= 0) {
            fprintf(stderr, "error encoding message in %zu-byte buffer\n",
                sizeof(proto_buf));
            break;  // failure
        }

//        rv = send_message(fd, proto_buf, n);
        rv = send_message(fd, proto_buf, ETH_ZLEN, &msg_out.ts);
        if (rv <= 0) {
            perror("send_message() failed");
            break;  // failure
        }

        rv = recv_message(fd, proto_buf, sizeof(proto_buf), &msg_in.ts);
        if (rv <= 0) {
            perror("recv_message() failed");
            break;  // failure
        }

        rv = parse_message(proto_buf, rv, &msg_in);
        if (rv <= 0) {
            perror("parse_message() failed");
            break;  // failure
        }

        // process message
        rv = process_message(&msg_in, &msg_out);

    }

    return rv;
}

#endif /* ZERO_COPY */

int
main(int argc, char *argv[])
{
    int rv, fd;

    // set new default protocol options
    proto_opt.family = AF_PACKET;
    proto_opt.sock_type = SOCK_RAW;
    proto_opt.eth_proto = ETH_P_DALE;

    rv = parse_args(&argc, argv);
    if (rv != 0) return rv;

    fputs(argv[0], stdout);
    print_proto_opt(stdout);

    if (proto_opt.if_index <= 0) {
        fprintf(stderr, "usage: %s if=<interface>\n", argv[0]);
        return 1;
    }

    // print real-time resolution
    struct timespec ts;
    if (clock_getres(CLOCK_REALTIME, &ts) < 0) {
        perror("clock_getres() failed");
        return -1;
    }
    printf("CLOCK_REALTIME resolution %zu.%09ld\n",
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
//    print_mac_addr(stdout, "eth_remote = ", eth_remote);
    print_mac_addr(stdout, "eth_local = ", eth_local);

    rv = bind_socket(fd);
    if (rv < 0) {
        perror("bind_socket() failed");
        return -1;  // failure
    }

    rv = server(fd);

    close(fd);

    return rv;
}
