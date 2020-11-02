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
#if 0
    bpf_printk("print_kern.o\n");
#else
    __u32 len = ctx->data_end - ctx->data;
    void *end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    __u64 *ptr = data;

    bpf_printk("packet len=%lu\n", len);
    if (data + (4 * sizeof(__u64)) > end) return XDP_DROP;  // frame too small
    bpf_printk("[0] %llx\n", __builtin_bswap64(ptr[0]));
    bpf_printk("[1] %llx\n", __builtin_bswap64(ptr[1]));
    bpf_printk("[2] %llx\n", __builtin_bswap64(ptr[2]));
    bpf_printk("[3] %llx\n", __builtin_bswap64(ptr[3]));
#endif
    return XDP_PASS;  // pass frame on to networking stack
}

char __license[] SEC("license") = "GPL";

