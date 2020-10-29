/*
 * live_kern.c -- XDP in-kernel eBPF filter
 *
 * Implement link-liveness protocol in XDP
 */
#include <stddef.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include "bpf_helpers.h"
#include "bpf_endian.h"

#define USE_BPF_MAPS 1  // monitor/control from userspace

#if USE_BPF_MAPS
#include <iproute2/bpf_elf.h>

struct bpf_elf_map liveness_map SEC("maps") = {
    .type       = BPF_MAP_TYPE_ARRAY,
    .size_key   = sizeof(__u32),
    .size_value = sizeof(__u64),
    .pinning    = PIN_GLOBAL_NS,
    .max_elem   = 4,
};
#endif /* USE_BPF_MAPS */

#define USE_CODE_C 1  // include encode/decode helpers

#if USE_CODE_C
#include "code.c"  // data encoding/decoding
#else
#include "code.h"  // data encoding/decoding
#endif

#define PERMISSIVE 1  // allow non-protocol packets to pass through

#define ETH_P_DALE (0xDA1E)

#ifndef __inline
#define __inline  inline __attribute__((always_inline))
#endif

static __inline void swap_mac_addrs(void *ethhdr)
{
    __u16 tmp[3];
    __u16 *eth = ethhdr;

    __builtin_memcpy(tmp, eth, 6);
    __builtin_memcpy(eth, eth + 3, 6);
    __builtin_memcpy(eth + 3, tmp, 6);
}

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

static int next_seq_num(int seq_num)
{
#if USE_BPF_MAPS
    __u32 key;
    __u64 *value_ptr;

    key = 3;
    value_ptr = bpf_map_lookup_elem(&liveness_map, &key);
    if (value_ptr) {
        __sync_fetch_and_add(value_ptr, 1);
        seq_num = *value_ptr;
    } else {
        ++seq_num;
    }
#else
    ++seq_num;
#endif
    return seq_num;
}

static int handle_message(__u8 *data, __u8 *end)
{
    int offset = ETH_HLEN;
    int size = 0;
    int state;
    int other;
    int seq_num;
#if USE_CODE_C
    int n;
#endif

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
#if USE_CODE_C
    n = parse_int_n(data + offset, end, &seq_num, 2);
//    bpf_printk("n=%d seq_num=%d\n", n, (int)seq_num);
    if (n <= 0) return XDP_DROP;  // parse error
    offset += n;
#else
    seq_num = SMOL2INT(data[offset++]);
    if ((seq_num < SMOL_MIN) || (seq_num > SMOL_MAX)) {
        return XDP_DROP;  // bad size
    }
#endif

    bpf_printk("%d,%d #%d <--\n", state, other, seq_num);

    // calculate new state
    state = other;  // swap self <-> other
    other = next_state(state);
    seq_num = next_seq_num(seq_num);
//    bpf_printk("state=%d other=%d seq_num=%d\n", state, other, seq_num);

    // prepare reply message
    swap_mac_addrs(data);
    offset = ETH_HLEN;
    data[offset++] = array;
    data[offset++] = INT2SMOL(ETH_ZLEN - (ETH_HLEN + 2));  // max array size
    int content = offset;
    __builtin_memset((data + content), 0xFF, ETH_ZLEN - (ETH_HLEN + 2));
    data[offset++] = INT2SMOL(state);
    data[offset++] = INT2SMOL(other);
#if USE_CODE_C
    n = code_int_n(data + offset, end, seq_num, 2);
    if (n <= 0) return XDP_DROP;  // coding error
    offset += n;
#else
    data[offset++] = INT2SMOL(seq_num);
#endif
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

