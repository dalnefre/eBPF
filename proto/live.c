/*
 * live.c -- Link liveness protocol
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

//static BYTE proto_buf[256];  // message-transfer buffer
static BYTE proto_buf[64];  // message-transfer buffer
//static BYTE proto_buf[ETH_ZLEN];  // message-transfer buffer
//static BYTE proto_buf[ETH_MIN_MTU];  // message-transfer buffer

typedef struct live_msg {
    int         state;      // self state
    int         other;      // other state
    int         count;      // message count
} live_msg_t;

static BYTE eth_remote[ETH_ALEN] =  { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static BYTE eth_local[ETH_ALEN] =   { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static int
next_state(int state)
{
    switch (state) {
    case 0:  return 1;
    case 1:  return 2;
    case 2:  return 1;
    case 3:  return 2;  // reject ait
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
    default: return 0;
    }
}
#endif

static int
print_resolution(char *label, clockid_t clock)
{
    struct timespec ts;

    int rv = clock_getres(CLOCK_REALTIME, &ts);
    if (rv >= 0) {
        printf("%sresolution %zu.%09ld\n",
            label, (size_t)ts.tv_sec, ts.tv_nsec);
    }
    return rv;
}

static int
print_timestamp(char *label, clockid_t clock)
{
    struct timespec ts;

    int rv = clock_gettime(CLOCK_REALTIME, &ts);
    if (rv >= 0) {
        printf("%stimestamp %zu.%09ld\n",
            label, (size_t)ts.tv_sec, ts.tv_nsec);
    }
    return rv;
}

size_t
create_message(void *data, size_t size, live_msg_t *msg)
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
    if (bstr_close_array(&meta) < 0) return 0;

    n = (meta.end - meta.start);  // number of bytes written to buffer
    if (n <= 0) return 0;  // error
    offset += n;

    return offset;
}

int
send_message(int fd, void *data, size_t size)
{
    struct sockaddr_storage address;
    socklen_t addr_len;
    int n; 

    struct sockaddr *addr = set_sockaddr(&address, &addr_len); 
    DEBUG(dump_sockaddr(stdout, addr, addr_len));

    print_timestamp("REALTIME: (send) ", CLOCK_REALTIME);

    n = sendto(fd, data, size, 0, addr, addr_len);
    if (n < 0) return n;  // sendto error

    DEBUG(dump_sockaddr(stdout, addr, addr_len));

#if 1
    fprintf(stdout, "Message[%d] --> \n", n);
    hexdump(stdout, data, size);
#endif

    return n;
}

int
recv_message(int fd, void *data, size_t limit)
{
    struct sockaddr_storage address;
    socklen_t addr_len;
    int n; 

    struct sockaddr *addr = clr_sockaddr(&address, &addr_len); 
    n = recvfrom(fd, data, limit, 0, addr, &addr_len);
    if (n < 0) return n;  // recvfrom error

    if (filter_message(addr, data, n)) {
        return n;  // early (succesful) exit
    }

    DEBUG(dump_sockaddr(stdout, addr, addr_len));

    print_timestamp("REALTIME: (recv) ", CLOCK_REALTIME);

#if 1
    fprintf(stdout, "Message[%d] <-- \n", n);
    hexdump(stdout, proto_buf, (n < 0 ? limit : n));
#endif

    return n;
}

int
parse_message(void *data, size_t limit, live_msg_t *msg_in)
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

    offset = bstr.end - bstr.base;  // update final offset
    return offset;
}

int
process_message(live_msg_t *in, live_msg_t *out)
{
    // act on message received
    out->state = in->other;
    out->other = next_state(out->state);
#if SHARED_COUNT
    out->count = in->count + 1;  // update message counter
#else
    out->count = out->count + 1;  // update message counter
#endif

    printf("process_message: %d,%d #%d -> %d,%d #%d\n",
        in->state, in->other, in->count,
        out->state, out->other, out->count);

    if (out->count > 13) {
        return 0;  // FIXME: halt ping/pong!
    }

    return 1;  // continue...
}

int
server()
{
    int fd, rv = -1;
    live_msg_t msg_out = {
        .state = 0,
        .other = 0,
        .count = 0,
    };
    live_msg_t msg_in;

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
        rv = send_message(fd, proto_buf, ETH_ZLEN);
        if (rv <= 0) {
            perror("send_message() failed");
            break;  // failure
        }

        rv = recv_message(fd, proto_buf, sizeof(proto_buf));
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

    print_resolution("REALTIME: ", CLOCK_REALTIME);

    rv = server();

    return rv;
}
