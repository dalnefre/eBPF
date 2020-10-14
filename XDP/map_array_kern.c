/*
 * map_array_kern.c -- XDP in-kernel eBPF filter
 *
 * Report datagram stats to shared ARRAY MAP, visible from userspace.
 */
#include <stddef.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <iproute2/bpf_elf.h>
#include "bpf_helpers.h"
#include "bpf_endian.h"

struct bpf_elf_map datagram_stat_map SEC("maps") = {
    .type       = BPF_MAP_TYPE_ARRAY,
    .size_key   = sizeof(__u32),
    .size_value = sizeof(__u64),
    .pinning    = PIN_GLOBAL_NS,
    .max_elem   = 2,
};

SEC("prog")
int xdp_filter(struct xdp_md *ctx)
{
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    __u64 data_len = data_end - data;
    __u32 key;
    __u64 *value_ptr;

    if (data + ETH_ZLEN > data_end) {
        return XDP_DROP;  // frame too small!
    }
    struct ethhdr *eth = data;
    __u16 eth_proto = bpf_ntohs(eth->h_proto);
    __u8 *pkt_data = data + ETH_HLEN;
    __u64 pkt_len = data_end - (void *)pkt_data;

    bpf_printk("data_len=%llu eth_proto=0x%x pkt_len=%llu\n",
        data_len, eth_proto, pkt_len);

    key = 0;
    value_ptr = bpf_map_lookup_elem(&datagram_stat_map, &key);
    if (value_ptr) {
        __sync_fetch_and_add(value_ptr, 1);
    }

    key = 1;
    value_ptr = bpf_map_lookup_elem(&datagram_stat_map, &key);
    if (value_ptr) {
        __sync_fetch_and_add(value_ptr, pkt_len);
    }

    return XDP_PASS;  // pass frame on to networking stack
}

char __license[] SEC("license") = "GPL";

