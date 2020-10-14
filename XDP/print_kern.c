/*
 * print_kern.c -- XDP in-kernel eBPF filter
 *
 * Print kernel trace message.
 */
#include <linux/bpf.h>

#define SEC(NAME) __attribute__((section(NAME), used))

#define BPF_FUNC(NAME, ...) (*bpf_##NAME)(__VA_ARGS__) = (void *)BPF_FUNC_##NAME

static int BPF_FUNC(trace_printk, const char *fmt, int fmt_size, ...);

/* helper macro to print out debug messages */
#define bpf_printk(fmt, ...)				\
({							\
	char ____fmt[] = fmt;				\
	bpf_trace_printk(____fmt, sizeof(____fmt),	\
			 ##__VA_ARGS__);		\
})

SEC("prog")
int xdp_filter(struct xdp_md *ctx)
{
    bpf_printk("print_kern.o\n");
    return XDP_PASS;  // pass frame on to networking stack
}

char __license[] SEC("license") = "GPL";

