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
#define UNALIGNED  0  // assume unaligned access for packet data
#define USE_MEMCPY 1  // use __built_in_memcpy() for block copies
#define LOG_RESULT 0  // log result code for all protocol packets
#define LOG_PROTO  1  // log all protocol messages exchanged
#define LOG_AIT    1  // log each AIT sent/recv
#define TRACE_MSG  0  // log raw message data (8 octets)
#define TRACE_AIT  1  // log ait transfer status

#define ETH_P_DALE (0xDA1E)

#define AIT_EMPTY  (-1)

#ifndef __inline
#define __inline  inline __attribute__((always_inline))
#endif

static __inline void
copy_ait(void *dst, void *src)
{
#if UNALIGNED
#if USE_MEMCPY
    __builtin_memcpy(dst, src, 8);
#else
#error No implementation for copy_ait()
#endif
#else
    *((__u64 *) dst) = *((__u64 *) src);
#endif
}

#if TRACE_MSG
static __inline void
trace_msg_data(__u8 *data)
{
    __u64 x;

    copy_ait(&x, data + 16);  // MSG_DATA_OFS = 16
    bpf_printk("msg = 0x%llx...\n", __builtin_bswap64(x));
}
#else
#define trace_msg_data(x) /* removed */
#endif /* TRACE_MSG */

static __inline void
copy_mac_addr(void *dst, void *src)
{
    __u16 *d = dst;
    __u16 *s = src;

    d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
}

static __inline void
swap_mac_addrs(void *ethhdr)
{
    __u16 tmp[3];
    __u16 *eth = ethhdr;

    __u32 key = 2;
    __u64 *value_ptr = bpf_map_lookup_elem(&ait_map, &key);
    if (value_ptr && (*value_ptr != 0)) {
        copy_mac_addr(eth + 0, eth + 3);
        copy_mac_addr(eth + 3, value_ptr);
    } else {
        copy_mac_addr(tmp + 0, eth + 0);
        copy_mac_addr(eth + 0, eth + 3);
        copy_mac_addr(eth + 3, tmp + 0);
    }
}

#define BUSY_FLAG (0x80)

static __inline __u64 *
acquire_ait()
{
    __u32 key;
    __u64 *ptr;
    __u8 *bp = NULL;

    key = 2;
    ptr = bpf_map_lookup_elem(&ait_map, &key);
    if (ptr) {
        bp = (__u8 *)ptr;
        if (bp[7] & BUSY_FLAG) {
#if TRACE_AIT
            bpf_printk("AIT busy!\n");
#endif
            return NULL;  // ait busy
        }
    }
    key = 0;
    ptr = bpf_map_lookup_elem(&ait_map, &key);
    if (ptr && (*ptr != AIT_EMPTY)) {
        if (bp) {
#if TRACE_AIT
            bpf_printk("AIT set busy.\n");
#endif
            bp[7] |= BUSY_FLAG;  // set ait busy
        }
#if TRACE_AIT
        bpf_printk("AIT start xfer\n");
#endif
        return ptr;  // transfer ait
    }
    return NULL;  // no ait
}

static __inline int
clear_outbound()
{
    __u32 key;
    __u64 *ptr;
    __u8 *bp = NULL;
    __u64 ait = AIT_EMPTY;

    key = 2;
    ptr = bpf_map_lookup_elem(&ait_map, &key);
    if (ptr) {
        bp = (__u8 *)ptr;
#if TRACE_AIT
        bpf_printk("AIT clear busy.\n");
#endif
        bp[7] &= ~BUSY_FLAG;  // clear ait busy
    }
    key = 0;
    int rv = bpf_map_update_elem(&ait_map, &key, &ait, BPF_ANY);
#if TRACE_AIT
        bpf_printk("AIT xfer done (rv=%d)\n", rv);
#endif
    return rv;
}

static __inline int
release_ait(__u64 ait)
{
    __u32 key = 1;
    __u64 *value_ptr = bpf_map_lookup_elem(&ait_map, &key);
    if (value_ptr && (*value_ptr == AIT_EMPTY)) {
        return bpf_map_update_elem(&ait_map, &key, &ait, BPF_ANY);
    }
    return -1;  // output slot already full
}

static __inline int
get_seq_num(int seq_num)
{
    __u32 n = seq_num;
    __u32 key = 3;
    __u64 *value_ptr = bpf_map_lookup_elem(&ait_map, &key);
    if (value_ptr) {
        n = *value_ptr;  // truncate to 32 bits
    }
    return n;
}

static __inline int
set_seq_num(int seq_num)
{
    __u32 key = 3;
    __u64 value = seq_num;
    if (bpf_map_update_elem(&ait_map, &key, &value, BPF_ANY) < 0) {
        seq_num = 0;  // default to zero
    }
    return seq_num;
}

static __inline int
update_seq_num(int seq_num)
{
    __u32 n = get_seq_num(seq_num);
    return set_seq_num(++n);
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

#define INIT_STATE    (n_0)
#define PING_STATE    (n_1)
#define PONG_STATE    (n_2)
#define GOT_AIT_STATE (n_3)
#define ACK_AIT_STATE (n_4)
#define ACK_ACK_STATE (n_5)
#define PROCEED_STATE (n_6)

static __inline void
live_msg_fmt(__u8 *data, __u8 state)
{
    data[OTHER_OFS] = state;
    data[MSG_LEN_OFS] = n_6;
    data[BLOB_OFS] = null;
    data[BLOB_LEN_OFS] = null;
}

static __inline void
ait_msg_fmt(__u8 *data, __u8 state)
{
    data[OTHER_OFS] = state;
    data[MSG_LEN_OFS] = n_24;
    data[BLOB_OFS] = octets;
    data[BLOB_LEN_OFS] = n_16;
}

static int
handle_message(__u8 *data, __u8 *end)
{
    __u8 b;
    __u16 n;
    __u64 i = AIT_EMPTY;
    __u64 u = AIT_EMPTY;

    if (data + MSG_END_OFS > end) return XDP_DROP;  // message too small
    if (data[MESSAGE_OFS] != array) return XDP_DROP;  // require array
    if (data[COUNT_OFS] != p_int_0) return XDP_DROP;  // require +INT (pad=0)
    if (data[COUNT_LEN_OFS] != n_2) return XDP_DROP;  // require size=2
    n = (data[COUNT_MSB_OFS] << 8) | data[COUNT_LSB_OFS];
    b = data[OTHER_OFS];
#if LOG_PROTO
    bpf_printk("%d,%d #%d <--\n", SMOL2INT(data[STATE_OFS]), SMOL2INT(b), n);
#endif
    if (data[MSG_LEN_OFS] == n_6) {  // live len = 6
        // liveness message
        switch (b) {
            case INIT_STATE: {  // init
                live_msg_fmt(data, PING_STATE);
                break;
            }
            case PING_STATE: {  // ping
                data[OTHER_OFS] = PONG_STATE;
                if (b < data[STATE_OFS]) {  // reverse transition
                    __u64 *p = acquire_ait();
                    if (p && (*p != AIT_EMPTY)) {  // outbound ait?
                        i = *p;
                        ait_msg_fmt(data, GOT_AIT_STATE);
                    }
                } else {  // forward transition (init)
                    __u32 m = set_seq_num(n);
                    n = set_seq_num((m & ~0xFFFF) | n);
#if LOG_AIT
                    bpf_printk("INIT: pkt #%d\n", m);
#endif
                }
                break;
            }
            case PONG_STATE: {  // pong
                data[OTHER_OFS] = PING_STATE;
                if (b > data[STATE_OFS]) {  // forward transition
                    __u64 *p = acquire_ait();
                    if (p && (*p != AIT_EMPTY)) {  // outbound ait?
                        i = *p;
                        ait_msg_fmt(data, GOT_AIT_STATE);
                    }
                }
                break;
            }
            default: return XDP_DROP;  // bad state
        }
    } else if (data[MSG_LEN_OFS] == n_24) {  // ait len = 6 + 18
        // message carries AIT
        if (data[BLOB_OFS] != octets) return XDP_DROP;  // require octets
        if (data[BLOB_LEN_OFS] != n_16) return XDP_DROP;  // require size=16
        copy_ait(&u, data + AIT_I_OFS);
        switch (b) {
            case GOT_AIT_STATE: {
                if (b < data[STATE_OFS]) {  // reverse
                    live_msg_fmt(data, PONG_STATE);
                } else {
                    ait_msg_fmt(data, ACK_AIT_STATE);
                }
                break;
            }
            case ACK_AIT_STATE: {
                if (b < data[STATE_OFS]) {  // reverse
                    ait_msg_fmt(data, GOT_AIT_STATE);
                } else {
                    ait_msg_fmt(data, ACK_ACK_STATE);
                }
                copy_ait(&i, data + AIT_U_OFS);
                break;
            }
            case ACK_ACK_STATE: {
                if (release_ait(u) < 0) {  // release failed
                    ait_msg_fmt(data, ACK_AIT_STATE);  // reverse
                } else {
#if LOG_AIT
                    bpf_printk("RCVD: 0x%llx\n", __builtin_bswap64(u));
#endif
                    ait_msg_fmt(data, PROCEED_STATE);
                }
                break;
            }
            case PROCEED_STATE: {
                if (clear_outbound() < 0) return XDP_DROP;  // clear failed
#if LOG_AIT
                copy_ait(&i, data + AIT_U_OFS);
                bpf_printk("SENT: 0x%llx\n", __builtin_bswap64(i));
#endif
                live_msg_fmt(data, PING_STATE);
                i = u = AIT_EMPTY;  // clear ait
                break;
            }
            default: return XDP_DROP;  // bad state
        }
    } else {
        return XDP_DROP;  // bad message length
    }
    // common processing
    trace_msg_data(data);
    n = update_seq_num(n);
    swap_mac_addrs(data);
    copy_ait(data + AIT_I_OFS, &i);
    copy_ait(data + AIT_U_OFS, &u);
    data[STATE_OFS] = b;  // processing state
    data[COUNT_LSB_OFS] = n;
    data[COUNT_MSB_OFS] = n >> 8;
    trace_msg_data(data);
#if LOG_PROTO
    bpf_printk("%d,%d #%d -->\n", SMOL2INT(b), SMOL2INT(data[OTHER_OFS]), n);
#endif
    return XDP_TX;  // send updated frame out on same interface
}

SEC("prog") int
xdp_filter(struct xdp_md *ctx)
{
#if LOG_RESULT
    __u32 data_len = ctx->data_end - ctx->data;
#endif
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
#if LOG_RESULT
    bpf_printk("proto=0x%x len=%lu rc=%d\n", eth_proto, data_len, rc);
    trace_msg_data(data);
#endif

    return rc;
}

char __license[] SEC("license") = "GPL";

