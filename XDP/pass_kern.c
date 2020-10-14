/*
 * pass_kern.c -- XDP in-kernel eBPF filter
 *
 * Always pass frame on to networking stack.
 */
#include <linux/bpf.h>

#ifndef __section
# define __section(NAME)  \
    __attribute__((section(NAME), used))
#endif

__section("prog")
int xdp_filter(struct xdp_md *ctx)
{
    return XDP_PASS;  // pass frame on to networking stack
}

char __license[] __section("license") = "GPL";

