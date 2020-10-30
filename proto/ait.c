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
#define LOG_PROTO    1  // log each protocol messages exchange
#define DUMP_PACKETS 0  // hexdump raw packets send/received

//static BYTE proto_buf[256];  // message-transfer buffer
static BYTE proto_buf[64];  // message-transfer buffer
//static BYTE proto_buf[ETH_ZLEN];  // message-transfer buffer
//static BYTE proto_buf[ETH_MIN_MTU];  // message-transfer buffer

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

static BYTE eth_remote[ETH_ALEN] =  { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static BYTE eth_local[ETH_ALEN] =   { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

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
send_message(int fd, void *data, size_t size, ait_msg_t *msg)
{
    struct sockaddr_storage address;
    socklen_t addr_len;
    int n; 

    struct sockaddr *addr = set_sockaddr(&address, &addr_len); 
    DEBUG(dump_sockaddr(stdout, addr, addr_len));

    clock_gettime(CLOCK_REALTIME, &msg->ts);

    n = sendto(fd, data, size, 0, addr, addr_len);
    if (n < 0) return n;  // sendto error

    DEBUG(dump_sockaddr(stdout, addr, addr_len));

#if DUMP_PACKETS
    fprintf(stdout, "%zu.%09ld Message[%d] --> \n",
        (size_t)msg->ts.tv_sec, (long)msg->ts.tv_nsec, n);
    hexdump(stdout, data, size);
#endif

    return n;
}

int
recv_message(int fd, void *data, size_t limit, ait_msg_t *msg)
{
    struct sockaddr_storage address;
    socklen_t addr_len;
    int n; 

    struct sockaddr *addr = clr_sockaddr(&address, &addr_len); 
    n = recvfrom(fd, data, limit, 0, addr, &addr_len);
    if (n < 0) return n;  // recvfrom error

    clock_gettime(CLOCK_REALTIME, &msg->ts);

    if (filter_message(addr, data, n)) {
        return n;  // early (succesful) exit
    }

    DEBUG(dump_sockaddr(stdout, addr, addr_len));

#if DUMP_PACKETS
    fprintf(stdout, "%zu.%09ld Message[%d] <-- \n",
        (size_t)msg->ts.tv_sec, (long)msg->ts.tv_nsec, n);
    hexdump(stdout, proto_buf, (n < 0 ? limit : n));
#endif

    return n;
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

    if (out->count > 13) {
        return 0;  // FIXME: halt ping/pong!
    }

    return 1;  // continue...
}

int
server()
{
    int fd, rv = -1;
    ait_msg_t msg_out = {
        .state = 0,
        .other = 0,
        .count = 0,
        .ait.i = null,
        .ait.u = null,
    };
    ait_msg_t msg_in;

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
    } else {
         rv = fd;
    }

    while (rv > 0) {

        rv = create_message(proto_buf, sizeof(proto_buf), &msg_out);
        if (rv <= 0) {
            fprintf(stderr, "error encoding message in %zu-byte buffer\n",
                sizeof(proto_buf));
            break;  // failure
        }

//        rv = send_message(fd, proto_buf, n);
        rv = send_message(fd, proto_buf, ETH_ZLEN, &msg_out);
        if (rv <= 0) {
            perror("send_message() failed");
            break;  // failure
        }

        rv = recv_message(fd, proto_buf, sizeof(proto_buf), &msg_in);
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

    close(fd);

    return rv;
}

int
main(int argc, char *argv[])
{
    // set new default protocol options
    proto_opt.family = AF_PACKET;
    proto_opt.sock_type = SOCK_RAW;
    proto_opt.eth_proto = ETH_P_DALE;

    int rv = parse_args(&argc, argv);
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

    rv = server();

    return rv;
}
