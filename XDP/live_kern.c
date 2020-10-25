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

#include "../proto/state.c"  // common code for shared state-machine

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

typedef struct ait {
    __u64 i;  // outbound
    __u64 u;  // inbound
} ait_t;

static void copy_mac(void *dst, void *src)
{
    __u16 *d = dst;
    __u16 *s = src;

    d[0] = s[0];
    d[1] = s[1];
    d[2] = s[2];
}

static void swap_mac_addrs(void *ethhdr)
{
    __u16 tmp[3];
    __u16 *eth = ethhdr;

    copy_mac(tmp, eth);
    copy_mac(eth, eth + 3);
    copy_mac(eth + 3, tmp);
}

static int next_state_ait(int state, ait_t *ait)
{
    // conditional state transition (checks for AIT)
    int next = next_state(state);
#if USE_BPF_MAPS
    __u32 key;
    __u64 *value_ptr;

    key = 0;
    value_ptr = bpf_map_lookup_elem(&liveness_map, &key);
    if (value_ptr) {
        if ((state == 1) || (state == 2)) {  // ping/pong
            __u8 *bp = (void *)value_ptr;

            if (bp[0] != null) {  // AIT ready to send
                next = 3;
            }
        } else if (state == 6) {  // AIT completed
            *value_ptr = -1;
        }
        ait->i = *value_ptr;
    }

    key = 1;
    value_ptr = bpf_map_lookup_elem(&liveness_map, &key);
    if (value_ptr) {
        if (state == 5) {  // AIT acknowledged
            *value_ptr = ait->u;
        } else {
            ait->u = *value_ptr;
        }
    }
#endif
    return next;
}

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
    __s16 seq_num;
    ait_t ait = { null, null };
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
    n = parse_int16(data + offset, end, &seq_num);
//    bpf_printk("n=%d seq_num=%d\n", n, (int)seq_num);
    if (n <= 0) return XDP_DROP;  // parse error
    offset += n;

    // get `ait` field(s)
//    bpf_printk("data+%d: 0x%x 0x%x\n", offset, data[offset], data[offset+1]);
    if (data[offset++] != octets) return XDP_DROP;  // require raw bytes
    if (data[offset++] != n_16) return XDP_DROP;  // require size = 16
//    bpf_printk("octets n_16 offset=%d\n", offset);
    n = bytes_to_u64(data + offset, end, &ait.i);
    if (n <= 0) return XDP_DROP;  // parse error
    offset += n;
    n = bytes_to_u64(data + offset, end, &ait.u);
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
    other = next_state_ait(state, &ait);
    seq_num = next_seq_num(seq_num);
//    bpf_printk("state=%d other=%d seq_num=%d\n", state, other, seq_num);

    // prepare reply message
    swap_mac_addrs(data);
    offset = ETH_HLEN;
    data[offset++] = array;
    data[offset++] = INT2SMOL(ETH_ZLEN - (ETH_HLEN + 2));  // max array size
    int content = offset;
    data[offset++] = INT2SMOL(state);
    data[offset++] = INT2SMOL(other);
#if USE_CODE_C
    n = code_int16(data + offset, end, seq_num);
    if (n <= 0) return XDP_DROP;  // coding error
    offset += n;
    data[offset++] = octets;  // raw bytes
    data[offset++] = n_16;  // size = 16
    n = u64_to_bytes(data + offset, end, ait.i);
    if (n <= 0) return XDP_DROP;  // coding error
    offset += 8;
    n = u64_to_bytes(data + offset, end, ait.u);
    if (n <= 0) return XDP_DROP;  // coding error
    offset += 8;
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

