/*
 * multi_kern.c -- XDP in-kernel eBPF filter
 *
 * Multiple XDP programs in named sections.
 *
 * To load the program in section `xdp_pass`:
 * ```
 * # ip link set dev lo xdp obj multi_kern.o sec xdp_pass
 * ```
 */
#include <linux/bpf.h>
#include "bpf_helpers.h"

SEC("xdp_pass")
int xdp_pass_filter(struct xdp_md *ctx)
{
    return XDP_PASS;  // pass frame on to networking stack
}

SEC("xdp_drop")
int xdp_drop_filter(struct xdp_md *ctx)
{
    return XDP_DROP;  // drop frame immediately!
}

SEC("xdp_abort")
int xdp_abort_filter(struct xdp_md *ctx)
{
    return XDP_ABORTED;  // signal error
    /* `xdp:xdp_exception` tracepoint triggered */
}

char __license[] SEC("license") = "GPL";

