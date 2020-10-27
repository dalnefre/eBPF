/*
 * ait_kern.c -- XDP in-kernel eBPF filter
 *
 * Implement atomic information transfer protocol in XDP
 */
#include <stddef.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include "bpf_helpers.h"
#include "bpf_endian.h"

#include <iproute2/bpf_elf.h>

struct bpf_elf_map ait_map SEC("maps") = {
    .type       = BPF_MAP_TYPE_ARRAY,
    .size_key   = sizeof(__u32),
    .size_value = sizeof(__u64),
    .pinning    = PIN_GLOBAL_NS,
    .max_elem   = 4,
};

#include "code.c"  // data encoding/decoding

#define PERMISSIVE 1  // allow non-protocol packets to pass through

#define ETH_P_DALE (0xDA1E)

typedef struct ait {
    __u64 i;  // outbound
    __u64 u;  // inbound
} ait_t;

#ifndef __inline
#define __inline  inline __attribute__((always_inline))
#endif

static __inline void swap_mac_addrs(void *ethhdr)
{
    __u8 tmp[6];
    __u8 *eth = ethhdr;

    __builtin_memcpy(tmp, eth, 6);
    __builtin_memcpy(eth, eth + 6, 6);
    __builtin_memcpy(eth + 6, tmp, 6);
}

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

static int next_state_ait(int state, ait_t *ait)
{
    // conditional state transition (checks for AIT)
    __u32 key;
    __u64 *value_ptr;

    int next = next_state(state);
    ait->u = ait->i;

    key = 0;
    value_ptr = bpf_map_lookup_elem(&ait_map, &key);
    if (value_ptr) {
        if ((state == 1) || (state == 2)) {  // ping/pong
            __u8 *bp = (void *)value_ptr;

            if (bp[0] != null) {  // AIT ready to send
                next = 3;
            }
        } else if (state == 6) {  // AIT completed
            bpf_printk("SENT: 0x%llx\n", __builtin_bswap64(*value_ptr));
            *value_ptr = -1;
        }
        ait->i = *value_ptr;
    }

    key = 1;
    value_ptr = bpf_map_lookup_elem(&ait_map, &key);
    if (value_ptr) {
        if (state == 5) {  // AIT acknowledged
            bpf_printk("RCVD: 0x%llx\n", __builtin_bswap64(ait->u));
            *value_ptr = ait->u;
        } else {
            ait->u = *value_ptr;
        }
    }
    return next;
}

static int next_seq_num(int seq_num)
{
    __u32 key;
    __u64 *value_ptr;

    key = 3;
    value_ptr = bpf_map_lookup_elem(&ait_map, &key);
    if (value_ptr) {
        __sync_fetch_and_add(value_ptr, 1);
        seq_num = *value_ptr;
    } else {
        ++seq_num;
    }
    return seq_num;
}

static int handle_message(__u8 *data, __u8 *end)
{
    int offset = ETH_HLEN;
    int size = 0;
    int state;
    int other;
    __s16 seq_num;
    ait_t ait = { null, null };
    int n;

    if (data + offset >= end) return XDP_DROP;  // out of bounds
    if (data[offset++] != array) return XDP_DROP;  // bad message type

    // get array size (in bytes)
    size = SMOL2INT(data[offset++]);
    if ((size < SMOL_MIN) || (size > SMOL_MAX)) {
        return XDP_DROP;  // bad size
    }
//    bpf_printk("array size=%d\n", size);
    if (data + offset + size > end) return XDP_DROP;  // array to large

    // get `state` field
    state = SMOL2INT(data[offset++]);
    if ((state < 0) || (state > 6)) {
        return XDP_DROP;  // bad state
    }

    // get `other` field
    other = SMOL2INT(data[offset++]);
    if ((other < 0) || (other > 6)) {
        return XDP_DROP;  // bad other
    }

//    bpf_printk("state=%d other=%d\n", state, other);

    // get `seq_num` field
    n = parse_int16(data + offset, end, &seq_num);
//    bpf_printk("n=%d seq_num=%d\n", n, (int)seq_num);
    if (n <= 0) return XDP_DROP;  // parse error
    offset += n;

    // get `ait` field(s)
    if (other > 2) {
//        bpf_printk("data+%d: 0x%x 0x%x\n", offset, data[offset], data[offset+1]);
        if (data[offset++] != octets) return XDP_DROP;  // require raw bytes
        if (data[offset++] != n_16) return XDP_DROP;  // require size = 16
//        bpf_printk("octets n_16 offset=%d\n", offset);
        ait.i = bytes_to_int64(data + offset);
        offset += 8;
        ait.u = bytes_to_int64(data + offset);
        offset += 8;
    }

    bpf_printk("%d,%d #%d <--\n", state, other, seq_num);

    // calculate new state
    state = other;  // swap self <-> other
    other = next_state_ait(state, &ait);
    seq_num = next_seq_num(seq_num);
//    bpf_printk("state=%d other=%d seq_num=%d\n", state, other, seq_num);

    // prepare reply message
    swap_mac_addrs(data);
    offset = ETH_HLEN;
    data[offset++] = array;
    int max_content = ETH_ZLEN - (ETH_HLEN + 2);
    data[offset++] = INT2SMOL(max_content);  // max array size
    int content = offset;
    __builtin_memset((data + content), 0xFF, max_content);
    data[offset++] = INT2SMOL(state);
    data[offset++] = INT2SMOL(other);
    n = code_int16(data + offset, end, seq_num);
    if (n <= 0) return XDP_DROP;  // coding error
    offset += n;
    if (other > 2) {
        data[offset++] = octets;  // raw bytes
        data[offset++] = n_16;  // size = 16
        int64_to_bytes(data + offset, ait.i);
        offset += 8;
        int64_to_bytes(data + offset, ait.u);
        offset += 8;
    }
//    bpf_printk("content=%d offset=%d\n", content, offset);
    data[content - 1] = INT2SMOL(offset - content);  // final array size

    bpf_printk("%d,%d #%d -->\n", state, other, seq_num);

    return XDP_TX;  // send updated frame out on same interface
}

SEC("prog")
int xdp_filter(struct xdp_md *ctx)
{
//    __u32 data_len = ctx->data_end - ctx->data;
    void *end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;

    if (data + ETH_ZLEN > end) {
        return XDP_DROP;  // frame too small
    }
    struct ethhdr *eth = data;
    __u16 eth_proto = bpf_ntohs(eth->h_proto);
    if (eth_proto != ETH_P_DALE) {
#if PERMISSIVE
        return XDP_PASS;  // pass frame on to networking stack
#else
        return XDP_DROP;  // wrong protocol
#endif
    }

    int rc = handle_message(data, end);
//    bpf_printk("proto=0x%x len=%lu rc=%d\n", eth_proto, data_len, rc);

    return rc;
}

char __license[] SEC("license") = "GPL";

