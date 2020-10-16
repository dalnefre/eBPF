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

#define USE_CODE_C 1

#if USE_CODE_C
#include "code.c"  // data encoding/decoding
#else
#include "code.h"  // data encoding/decoding
#endif

#define ETH_P_DALE (0xDA1E)

#define PERMISSIVE 1  // allow non-protocol packets to pass through

static void copy_mac(void *dst, void* src)
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

static int handle_message(struct xdp_md *ctx)
{
    __u8 *msg_base = (void *)(long)ctx->data;
    __u8 *msg_start = msg_base + ETH_HLEN;
    __u8 *msg_limit = (void *)(long)ctx->data_end;

    __u8 *msg_cursor = msg_start;
    __u8 *msg_end = msg_limit;
    int size = 0;
    int count = -1;
#if USE_CODE_C
    int n;
#endif

    if (msg_cursor >= msg_end) return XDP_DROP;  // out of bounds
    __u8 b = *msg_cursor++;
    if (b == array) {

        // get array size (in bytes)
        if (msg_cursor >= msg_end) return XDP_DROP;  // out of bounds
        b = *msg_cursor++;
        size = SMOL2INT(b);
        if ((size < SMOL_MIN) || (size > SMOL_MAX)) {
            return XDP_DROP;  // bad size
        }
        msg_end = msg_cursor + size;  // limit to array contents
        if (msg_end > msg_limit) return XDP_DROP;  // out of bounds

    } else if (b == array_n) {

        // get array size (in bytes)
        if (msg_cursor >= msg_end) return XDP_DROP;  // out of bounds
        b = *msg_cursor++;
        size = SMOL2INT(b);
        if ((size < 0) || (size > SMOL_MAX)) {
            return XDP_DROP;  // bad size
        }
        msg_end = msg_cursor + size;  // limit to array contents
        if (msg_end > msg_limit) return XDP_DROP;  // out of bounds

        // get array element count
        if (msg_cursor >= msg_end) return XDP_DROP;  // out of bounds
        b = *msg_cursor++;
        count = SMOL2INT(b);
        if ((count < 0) || (count >= size)) {
            return XDP_DROP;  // bad count
        }

    } else {
        return XDP_DROP;  // bad message type
    }

    __u8 *msg_content = msg_cursor;  // start of array elements
//    bpf_printk("array size=%d count=%d\n", size, count);

    if (msg_cursor >= msg_end) return XDP_DROP;  // out of bounds
    b = *msg_cursor++;
    int state = SMOL2INT(b);
    if ((state < 0) || (state > 2)) {
        return XDP_DROP;  // bad state
    }

    // get `change` field
    if (msg_cursor >= msg_end) return XDP_DROP;  // out of bounds
    b = *msg_cursor++;
    int change = SMOL2INT(b);
    if ((change < SMOL_MIN) || (change > SMOL_MAX)) {
        return XDP_DROP;  // bad change
    }

    // get `seq_num` field
#if USE_CODE_C
    int seq_num;
    n = parse_int(msg_cursor, msg_end, &seq_num);
    if (n <= 0) return XDP_DROP;  // parse error
    msg_cursor += n;
#else
    if (msg_cursor >= msg_end) return XDP_DROP;  // out of bounds
    b = *msg_cursor++;
    int seq_num = SMOL2INT(b);
#endif
    if ((seq_num < 0) || (seq_num > 5)) {
        return XDP_DROP;  // bad seq
    }

    bpf_printk("%d (+ %d) #%d <--\n", state, change, seq_num);

    // calculate new state
    state += change;
    switch (state) {
        case 0: { change = +1; break; }
        case 1: { change = +1; break; }
        case 2: { change = -1; break; }
        default: return XDP_DROP;  // bad state
    }
    ++seq_num;

    // prepare reply message
    swap_mac_addrs(msg_base);
    msg_content[0] = INT2SMOL(state);
    msg_content[1] = INT2SMOL(change);
    msg_content[2] = INT2SMOL(seq_num);

    bpf_printk("%d (+ %d) #%d -->\n", state, change, seq_num);

    return XDP_TX;  // send updated frame out on same interface
}

SEC("prog")
int xdp_filter(struct xdp_md *ctx)
{
//    __u32 data_len = ctx->data_end - ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;

    if (data + ETH_ZLEN > data_end) {
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

    int rc = handle_message(ctx);
//    bpf_printk("proto=0x%x len=%lu rc=%d\n", eth_proto, data_len, rc);

    return rc;
}

char __license[] SEC("license") = "GPL";

