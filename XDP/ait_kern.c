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
#define TIMESTAMPS 0  // record packet processing timestamps
#define MONOLITHIC 1  // use straight-line code for packet handling
#define ZERO_COPY  1  // apply in-place edits to packet buffer
#define UNALIGNED  0  // assume unaligned access for packet data
#define USE_MEMCPY 1  // use __built_in_memcpy() for block copies
#define AIT_IN_MAP 1  // communicate AIT through BPF MAP
#define LOG_PROTO  0  // log all protocol messages exchanged
#define LOG_AIT    1  // log each AIT sent/recv

#define ETH_P_DALE (0xDA1E)

typedef struct ait {
    __u64 i;  // outbound
    __u64 u;  // inbound
} ait_t;

typedef struct ait_msg {
    int   state;  // self state
    int   other;  // other state
    int   count;  // message count
    ait_t ait;    // AIT buffers
#if TIMESTAMPS
    __u64 ts;     // timestamp
#endif
} ait_msg_t;

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

static __inline void
swap_mac_addrs(void *ethhdr)
{
    __u16 tmp[3];
    __u16 *eth = ethhdr;

    tmp[0] = eth[0]; tmp[1] = eth[1]; tmp[2] = eth[2];
    eth[0] = eth[3]; eth[1] = eth[4]; eth[2] = eth[5];
    eth[3] = tmp[0]; eth[4] = tmp[1]; eth[5] = tmp[2];
}

static __inline int
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
static __inline int
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

static __inline __u64 *
acquire_ait()
{
    __u32 key = 0;
    return bpf_map_lookup_elem(&ait_map, &key);
}

static __inline int
clear_outbound()
{
    __u32 key = 0;
    __u64 ait = -1;
    return bpf_map_update_elem(&ait_map, &key, &ait, BPF_ANY);
}

static __inline int
release_ait(__u64 ait)
{
    __u32 key = 1;
    return bpf_map_update_elem(&ait_map, &key, &ait, BPF_ANY);
}

static __inline int
update_seq_num(int seq_num)
{
    __u32 key = 3;
    __u64 *value_ptr = bpf_map_lookup_elem(&ait_map, &key);
    if (value_ptr) {
        __sync_add_and_fetch(value_ptr, 1);
        seq_num = *value_ptr;
    } else {
        ++seq_num;
    }
    return seq_num;
}

#if MONOLITHIC
#if ZERO_COPY
#define MESSAGE_OFS   (ETH_HLEN + 0)
#define MSG_LEN_OFS   (MESSAGE_OFS + 1)
#define MSG_DATA_OFS  (MESSAGE_OFS + 2)
#define STATE_OFS     (MSG_DATA_OFS + 0)
#define OTHER_OFS     (MSG_DATA_OFS + 1)
#define COUNT_OFS     (MSG_DATA_OFS + 2)
#define COUNT_LEN_OFS (COUNT_OFS + 1)
#define COUNT_LSB_OFS (COUNT_OFS + 2)
#define COUNT_MSB_OFS (COUNT_OFS + 3)
#define BLOB_OFS      (COUNT_OFS + 4)
#define BLOB_LEN_OFS  (BLOB_OFS + 1)
#define BLOB_AIT_OFS  (BLOB_OFS + 2)
#define AIT_SIZE      (8)
#define AIT_I_OFS     (BLOB_AIT_OFS + 0)
#define AIT_U_OFS     (BLOB_AIT_OFS + AIT_SIZE)
#define MSG_END_OFS   (AIT_U_OFS + AIT_SIZE)

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
    b = data[OTHER_OFS];
    n = (data[COUNT_MSB_OFS] << 8) | data[COUNT_LSB_OFS];
#if LOG_PROTO
    bpf_printk("%d,%d #%d <--\n", SMOL2INT(data[STATE_OFS]), SMOL2INT(b), n);
#endif
    if (data[MSG_LEN_OFS] == n_24) {  // ait len = 6 + 18
        // message carries AIT
        if (data[BLOB_OFS] != octets) return XDP_DROP;  // require octets
        if (data[BLOB_LEN_OFS] != n_16) return XDP_DROP;  // require size=16
        copy_ait(&u, data + AIT_I_OFS);
        switch (b) {
            case n_3: {  // got ait
                data[OTHER_OFS] = n_4;
                break;
            }
            case n_4: {  // ack ait
                data[OTHER_OFS] = n_5;
                copy_ait(&i, data + AIT_U_OFS);
                break;
            }
            case n_5: {  // ack ack
                data[OTHER_OFS] = n_6;
                if (release_ait(u) < 0) return XDP_DROP;  // release failed
#if LOG_AIT
                bpf_printk("RCVD: 0x%llx\n", __builtin_bswap64(u));
#endif
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
                bpf_printk("SENT: 0x%llx\n", __builtin_bswap64(i));
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
                break;
            }
            case n_2: {  // pong
                data[OTHER_OFS] = n_1;
                break;
            }
            default: return XDP_DROP;  // bad state
        }
        if (b != n_0) {  // check for ait
            __u64 *p = acquire_ait();
            if (p && (*p != -1)) {
                i = *p;
                data[OTHER_OFS] = n_3;
                data[MSG_LEN_OFS] = n_24;
                data[BLOB_OFS] = octets;
                data[BLOB_LEN_OFS] = n_16;
            }
        }
    }
    // common processing
    data[STATE_OFS] = b;  // processing state
    n = update_seq_num(n);
    data[COUNT_LSB_OFS] = n;
    data[COUNT_MSB_OFS] = n >> 8;
    swap_mac_addrs(data);
    copy_ait(data + AIT_I_OFS, &i);
    copy_ait(data + AIT_U_OFS, &u);
#if LOG_PROTO
    bpf_printk("%d,%d #%d -->\n", SMOL2INT(b), SMOL2INT(data[OTHER_OFS]), n);
#endif
    return XDP_TX;  // send updated frame out on same interface
}
#else /* !ZERO_COPY */
static int
next_state_ait(int state, ait_t *ait)
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

static int
handle_message(__u8 *data, __u8 *end)
{
    int offset = ETH_HLEN;
    int size = 0;
    int state;
    int other;
    int seq_num;
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
    n = parse_int_n(data + offset, end, &seq_num, 2);
//    bpf_printk("n=%d seq_num=%d\n", n, seq_num);
    if (n <= 0) return XDP_DROP;  // parse error
    offset += n;

    // get `ait` field(s)
    if (other > 2) {
//        bpf_printk("data+%d: 0x%x 0x%x\n", offset, data[offset], data[offset+1]);
        if (data[offset++] != octets) return XDP_DROP;  // require raw bytes
        if (data[offset++] != n_16) return XDP_DROP;  // require size = 16
//        bpf_printk("octets n_16 offset=%d\n", offset);
#if UNALIGNED
#if USE_MEMCPY
        __builtin_memcpy(&ait, data + offset, sizeof(ait));
        offset += sizeof(ait);
#else
        ait.i = bytes_to_int64(data + offset);
        offset += 8;
        ait.u = bytes_to_int64(data + offset);
        offset += 8;
#endif
#else
        ait.i = *(__u64 *)(data + offset);
        offset += 8;
        ait.u = *(__u64 *)(data + offset);
        offset += 8;
#endif
    }

#if LOG_PROTO
    bpf_printk("%d,%d #%d <--\n", state, other, seq_num);
#endif

    // calculate new state
    state = other;  // swap self <-> other
    other = next_state_ait(state, &ait);
    seq_num = (__s16)(seq_num + 1);
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
    n = code_int_n(data + offset, end, seq_num, 2);
    if (n <= 0) return XDP_DROP;  // coding error
    offset += n;
    if (other > 2) {
        data[offset++] = octets;  // raw bytes
        data[offset++] = n_16;  // size = 16
#if UNALIGNED
#if USE_MEMCPY
        __builtin_memcpy(data + offset, &ait, sizeof(ait));
        offset += sizeof(ait);
#else
        int64_to_bytes(data + offset, ait.i);
        offset += 8;
        int64_to_bytes(data + offset, ait.u);
        offset += 8;
#endif
#else
        *(__u64 *)(data + offset) = ait.i;
        offset += 8;
        *(__u64 *)(data + offset) = ait.u;
        offset += 8;
#endif
    }
//    bpf_printk("content=%d offset=%d\n", content, offset);
    data[content - 1] = INT2SMOL(offset - content);  // final array size

#if LOG_PROTO
    bpf_printk("%d,%d #%d -->\n", state, other, seq_num);
#endif

    return XDP_TX;  // send updated frame out on same interface
}
#endif /* ZERO_COPY */
#else /* !MONOLITHIC */
static int
parse_message(__u8 *data, __u8 *end, ait_msg_t *in)
{
    int n;

    if (data + ETH_ZLEN > end) return -1;  // packet too small
#if TIMESTAMPS
    in->ts = bpf_ktime_get_ns();
#endif

    data += ETH_HLEN;  // skip Ethernet header
    if (data + 2 > end) return -1;  // out of bounds
    if (data[0] != array) return -1;  // require array
    int size = SMOL2INT(data[1]);  // array size (in bytes)
//    bpf_printk("array size=%d\n", size);
    if ((size < 6) || (size > SMOL_MAX)) {
        return -1;  // bad size
    }

    data += 2;
    if (data + size > end) return -1;  // array to large

    // get `state` field
    n = SMOL2INT(data[0]);
    if ((n < 0) || (n > 6)) {
        return -1;  // bad state
    }
    in->state = n;

    // get `other` field
    n = SMOL2INT(data[1]);
    if ((n < 0) || (n > 6)) {
        return -1;  // bad other
    }
    in->other = n;

//    bpf_printk("state=%d other=%d\n", in->state, in->other);

    // get `seq_num` field
    if (data[2] != p_int_0) return -1;  // require +INT (pad=0)
    if (data[3] != n_2) return -1;  // require size = 2
    in->count = bytes_to_int16(data + 4);

    // get `ait` field(s)
    if (size > 6) {
        if (size < 6 + 18) return -1;  // AIT too small
        data += 6;
        if (data + 18 > end) return -1;  // FIXME: superflous check?
        if (data[0] != octets) return -1;  // require raw bytes
        if (data[1] != n_16) return -1;  // require size = 16
#if UNALIGNED
//        in->ait.i = bytes_to_int64(data + 2);
//        in->ait.u = bytes_to_int64(data + 10);
        __builtin_memcpy(&in->ait, data + 2, sizeof(in->ait));
#else
        in->ait.i = *(__u64 *)(data + 2);
        in->ait.u = *(__u64 *)(data + 10);
#endif
    }

#if LOG_PROTO
    bpf_printk("%d,%d #%d <--\n", in->state, in->other, in->count);
#endif
    return 0;
}

static int
process_message(ait_msg_t *in, ait_msg_t *out)
{
    __u32 key;
    __u64 *value_ptr;
    __u64 value;

    out->state = in->other;  // swap self <-> other
    out->other = next_state(out->state);
    out->ait.u = in->ait.i;

#if AIT_IN_MAP
#if 1
    switch (out->state) {
        case 1: // FALL-THRU
        case 2: {
            key = 0;
            value_ptr = bpf_map_lookup_elem(&ait_map, &key);
            if (value_ptr) {
                value = *value_ptr;
#if 1
                if (value != -1) {
                    out->other = 3;  // begin AIT send
                }
#else
                __u8 *bp = (__u8 *)&value;
                if (bp[0] != null) {  // AIT ready to send
                    out->other = 3;
                } else {
                    value = -1;
                }
#endif
                out->ait.i = value;
            }
            break;
        }
        case 3: {
            out->ait.i = -1;
            break;
        }
        case 4: {
            out->ait.i = in->ait.u;
            break;
        }
        case 5: {
            key = 1;
            value = out->ait.u;
            if (bpf_map_update_elem(&ait_map, &key, &value, BPF_ANY) < 0) {
                return -1;  // map update failed!
            }
            bpf_printk("RCVD: 0x%llx\n", __builtin_bswap64(value));
            out->ait.i = -1;
            break;
        }
        case 6: {
            value = in->ait.u;
            bpf_printk("SENT: 0x%llx\n", __builtin_bswap64(value));
            key = 0;
            value = -1;  // clear outbound AIT
            if (bpf_map_update_elem(&ait_map, &key, &value, BPF_ANY) < 0) {
                return -1;  // map update failed!
            }
            out->ait.i = value;
            break;
        }
    }
#else
    // outbound ait
    key = 0;
    value_ptr = bpf_map_lookup_elem(&ait_map, &key);
    if (value_ptr) {
        __u64 value = *value_ptr;
        if ((out->state == 1) || (out->state == 2)) {  // ping/pong
            __u8 *bp = (__u8 *)&value;

            if (bp[0] != null) {  // AIT ready to send
                out->other = 3;
            }
        } else if (out->state == 6) {  // AIT completed
            bpf_printk("SENT: 0x%llx\n", __builtin_bswap64(value));
            value = -1;  // clear AIT
            *value_ptr = value;  // FIXME: use map_update instead?
        }
        out->ait.i = value;
    }

    // inbound ait
    key = 1;
    value_ptr = bpf_map_lookup_elem(&ait_map, &key);
    if (value_ptr) {
        if (out->state == 5) {  // AIT acknowledged
            __u64 value = out->ait.u;
            bpf_printk("RCVD: 0x%llx\n", __builtin_bswap64(value));
            *value_ptr = value;  // FIXME: use map_update instead?
        }
    }
#endif
#endif /* AIT_IN_MAP */

    // sequence number
    __s16 seq_num = in->count + 1;
    key = 3;
    value_ptr = bpf_map_lookup_elem(&ait_map, &key);
    if (value_ptr) {
        __sync_fetch_and_add(value_ptr, 1);
        seq_num = *value_ptr;
    }
    out->count = seq_num;

//    bpf_printk("state=%d other=%d count=%d\n", out->state, out->other, out->count);
    return 0;  // FIXME: maybe return -1 if map_lookup fails?
}

static int
code_message(__u8 *data, __u8 *end, ait_msg_t *out)
{
    if (data + ETH_ZLEN > end) return -1;  // buffer too small
    swap_mac_addrs(data);

    data += ETH_HLEN;
    data[0] = array;
#if AIT_IN_MAP
    int size = (out->other < 3) ? 6 : 6 + 18;  // array size in bytes
#else
    int size = 6;  // array size in bytes
#endif
    data[1] = INT2SMOL(size);

    data += 2;
    data[0] = INT2SMOL(out->state);
    data[1] = INT2SMOL(out->other);
    data[2] = p_int_0;  // +INT (pad=0)
    data[3] = n_2;  // size = 2
    int16_to_bytes(data + 4, out->count);
#if AIT_IN_MAP
    if (out->other > 2) {
        data += 6;
        if (data + 18 > end) return -1;  // FIXME: superflous check?
        data[0] = octets;  // raw bytes
        data[1] = n_16;  // size = 16
#if UNALIGNED
//        int64_to_bytes(data + 2, out->ait.i);
//        int64_to_bytes(data + 10, out->ait.u);
        __builtin_memcpy(data + 2, &out->ait, sizeof(out->ait));
#else
        *(__u64 *)(data + 2) = out->ait.i;
        *(__u64 *)(data + 10) = out->ait.u;
#endif
    }
#endif /* AIT_IN_MAP */

#if TIMESTAMPS
    out->ts = bpf_ktime_get_ns();
#endif

#if LOG_PROTO
    bpf_printk("%d,%d #%d -->\n", out->state, out->other, out->count);
#endif
    return 0;
}

static int
handle_message(__u8 *data, __u8 *end)
{
    ait_msg_t msg_in, msg_out;

    if (parse_message(data, end, &msg_in) < 0) {
        return XDP_DROP;  // parsing error
    }

    if (process_message(&msg_in, &msg_out) < 0) {
        return XDP_DROP;  // processing error
    }

    if (code_message(data, end, &msg_out) < 0) {
        return XDP_DROP;  // coding error
    }

    return XDP_TX;  // send updated frame out on same interface
}
#endif /* MONOLITHIC */

SEC("prog") int
xdp_filter(struct xdp_md *ctx)
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

