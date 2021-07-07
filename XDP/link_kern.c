/*
 * link_kern.c -- XDP in-kernel eBPF filter
 *
 * Implement Liveness and AIT protocols in XDP
 */
#include <stddef.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include "bpf_helpers.h"
#include "bpf_endian.h"
#include "../include/code.h"
#include "../include/link.h"

#define PERMISSIVE   1  // allow non-protocol frames to pass through
#define LOG_LEVEL    2  // log level (0=none, 1=info, 2=debug, 3=trace)

#ifndef __inline
#define __inline  inline __attribute__((always_inline))
#endif

#define memcpy(dst,src,len)  __builtin_memcpy(dst, src, len);
#define memset(dst,val,len)  __builtin_memset(dst, val, len);


/* always print warnings and errors */
#define LOG_WARN(fmt, ...)  LOG_PRINT(0, (fmt), ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  LOG_PRINT(0, (fmt), ##__VA_ARGS__)
#define LOG_TEMP(fmt, ...)  LOG_PRINT(0, (fmt), ##__VA_ARGS__)
//#define LOG_TEMP(fmt, ...)  /* REMOVED */

#define LOG_PRINT(level, fmt, ...)  bpf_printk((fmt), ##__VA_ARGS__)
#define MAC_PRINT(level, tag, mac)  /* FIXME: NOT IMPLEMENTED */
#define HEX_DUMP(level, buf, len)   \
  bpf_printk("[%u] %llx %llx\n", (len), \
    __builtin_bswap64(((__u64 *)buf)[0]), \
    __builtin_bswap64(((__u64 *)buf)[1]));

#if (LOG_LEVEL < 1)
#define LOG_INFO(fmt, ...)  /* REMOVED */
#define HEX_INFO(buf, len)  /* REMOVED */
#else
#define LOG_INFO(fmt, ...)  LOG_PRINT(1, (fmt), ##__VA_ARGS__)
#define HEX_INFO(buf, len)  HEX_DUMP(1, (buf), (len))
#endif

#if (LOG_LEVEL < 2)
#define LOG_DEBUG(fmt, ...)  /* REMOVED */
#define HEX_DEBUG(buf, len)  /* REMOVED */
#else
#define LOG_DEBUG(fmt, ...)  LOG_PRINT(2, (fmt), ##__VA_ARGS__)
#define HEX_DEBUG(buf, len)  HEX_DUMP(2, (buf), (len))
#endif

#if (LOG_LEVEL < 3)
#define LOG_TRACE(fmt, ...)  /* REMOVED */
#define MAC_TRACE(tag, mac)  /* REMOVED */
#define HEX_TRACE(buf, len)  /* REMOVED */
#else
#define LOG_TRACE(fmt, ...)  LOG_PRINT(3, (fmt), ##__VA_ARGS__)
#define MAC_TRACE(tag, mac)  MAC_PRINT(3, (tag), (mac))
#define HEX_TRACE(buf, len)  HEX_DUMP(3, (buf), (len))
#endif


#include <iproute2/bpf_elf.h>

struct bpf_elf_map user_map SEC("maps") = {
    .type       = BPF_MAP_TYPE_ARRAY,
    .size_key   = sizeof(__u32),
    .size_value = sizeof(user_state_t),
    .pinning    = PIN_GLOBAL_NS,
    .max_elem   = 16,
};

user_state_t *
get_user_state(__u32 if_index)
{
    return bpf_map_lookup_elem(&user_map, &if_index);
}

struct bpf_elf_map link_map SEC("maps") = {
    .type       = BPF_MAP_TYPE_ARRAY,
    .size_key   = sizeof(__u32),
    .size_value = sizeof(link_state_t),
    .pinning    = PIN_GLOBAL_NS,
    .max_elem   = 16,
};

link_state_t *
get_link_state(__u32 if_index)
{
    return bpf_map_lookup_elem(&link_map, &if_index);
}

#include "../include/link_ssm.c"

SEC("prog") int
xdp_filter(struct xdp_md *ctx)
{
    __u32 data_len = ctx->data_end - ctx->data;
    void *end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    __u32 if_index = ctx->ingress_ifindex;

    if (data + ETH_ZLEN > end) {
        LOG_ERROR("frame too small. expect=%u, actual=%u\n",
            ETH_ZLEN, data_len);
        return XDP_DROP;  // frame too small
    }
    HEX_TRACE(data, data_len);
    struct ethhdr *eth = data;
    __u16 eth_proto = bpf_ntohs(eth->h_proto);
    if (eth_proto != ETH_P_DALE) {
#if PERMISSIVE
        return XDP_PASS;  // pass frame on to networking stack
#else
        LOG_WARN("wrong protocol. expect=0x%x, actual=0x%x\n",
            ETH_P_DALE, eth_proto);
        return XDP_DROP;  // wrong protocol
#endif
    }

    user_state_t *user = get_user_state(if_index);
    if (!user) {
        LOG_ERROR("failed loading if=%u user_state\n", if_index);
        return XDP_DROP;  // BPF Map failure
    }

    link_state_t *link = get_link_state(if_index);
    if (!link) {
        LOG_ERROR("failed loading if=%u link_state\n", if_index);
        return XDP_DROP;  // BPF Map failure
    }

    int rc = on_frame_recv(data, end, user, link);
    LOG_TRACE("recv: proto=0x%x len=%u rc=%d\n", eth_proto, data_len, rc);

    if (rc == XDP_TX) {
        memcpy(data, link->frame, ETH_ZLEN);  // copy frame to i/o buffer
    }

    return rc;
}

char __license[] SEC("license") = "GPL";

