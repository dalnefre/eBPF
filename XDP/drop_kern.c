/*
 * drop_kern.c -- XDP in-kernel eBPF filter
 *
 * Always drop frame immediately!
 */
#include <linux/bpf.h>

#define SEC(NAME) __attribute__((section(NAME), used))

SEC("prog")
int xdp_filter(struct xdp_md *ctx)
{
    return XDP_DROP;  // drop frame immediately!
}

char __license[] SEC("license") = "GPL";

