/*
 * ethertype_kern.c -- XDP in-kernel eBPF filter
 *
 * Report frame Ethertype in a kernel trace message.
 */
#include <stddef.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include "bpf_helpers.h"
#include "bpf_endian.h"

SEC("prog")
int xdp_filter(struct xdp_md *ctx)
{
    __u32 data_len = ctx->data_end - ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    struct ethhdr *eth = data;

    __u32 nh_off = sizeof(struct ethhdr);
    if (data + nh_off > data_end) {
        return XDP_DROP;  // frame too small!
    }
    __u16 eth_proto = bpf_ntohs(eth->h_proto);

    __u64 ktime_ns = bpf_ktime_get_ns();
    bpf_printk("eth_proto=0x%x data_len=%lu ktime_ns=%llu\n",
        eth_proto, data_len, ktime_ns);

    return XDP_PASS;  // pass frame on to networking stack
}

char __license[] SEC("license") = "GPL";

