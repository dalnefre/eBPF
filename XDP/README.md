# XDP Experiments

A set of experiments with using [eBPF](../README.md) programs attached to the XDP hook.

To compile these tools for your system and run unit tests:

```
$ make clean all test
```

**NOTE:** The `bpf_*.h` headers were originally copied from `~/dev/linux/tools/testing/selftest/bpf/` and modified to remove "unsupported" entry-points.

## Simple No-Op Filter

Let's start with one of the simplest possible XDP filters ([`pass_kern.c`](pass_kern.c)),
which simply passes every frame on to the networking stack.

```
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
```

We can compile this programs with `clang` using the `bpf` target.

```
$ clang -O2 -g -Wall -target bpf -c -o pass_kern.o pass_kern.c
```

This creates an object file `pass_kern.o` that can be examined the `llvm-objdump` tool.

```
$ llvm-objdump -S pass_kern.o

pass_kern.o:	file format ELF64-BPF

Disassembly of section prog:
xdp_filter:
; {
       0:	b7 00 00 00 02 00 00 00 	r0 = 2
; return XDP_PASS;  // pass frame on to networking stack
       1:	95 00 00 00 00 00 00 00 	exit
```

This simple program compiles to just two simple instructions.
The first loads `r0` with the immediate value `2`, representing the `XDP_PASS` verdict.
The second exits the program, return to the kernel,
where the `XDP_PASS` value in `r0` will cause the kernel to pass the packet on to the networking stack.

We want to load this program into the XDP hook for a link device,
so we use the `ip` tool list the available devices.

```
$ ip link list
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 1000
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 xdpgeneric qdisc pfifo_fast state UP mode DEFAULT group default qlen 1000
    link/ether b8:27:eb:f3:5a:d2 brd ff:ff:ff:ff:ff:ff
3: wlan0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP mode DORMANT group default qlen 1000
    link/ether b8:27:eb:a6:0f:87 brd ff:ff:ff:ff:ff:ff
```

Let's load the program into the XDP hook for the loopback device (`lo`).

```
$ sudo ip link set dev lo xdp obj pass_kern.o
```

We can verify that the program is loaded using the `ip` tool, as before.

```
$ ip link list
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 xdpgeneric qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 1000
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    prog/xdp id 13 
...
```

We can exercise the loopback device using the `ping` tool.

```
$ ping localhost
PING localhost(localhost (::1)) 56 data bytes
64 bytes from localhost (::1): icmp_seq=1 ttl=64 time=0.152 ms
64 bytes from localhost (::1): icmp_seq=2 ttl=64 time=0.134 ms
64 bytes from localhost (::1): icmp_seq=3 ttl=64 time=0.096 ms
64 bytes from localhost (::1): icmp_seq=4 ttl=64 time=0.087 ms
^C
--- localhost ping statistics ---
4 packets transmitted, 4 received, 0% packet loss, time 86ms
rtt min/avg/max/mdev = 0.087/0.117/0.152/0.027 ms
```

Finally, we can unload the program using the `ip` tool.

```
$ sudo ip link set dev lo xdp off
```

## Simple Drop Filter

This program takes a noticable action ([`drop_kern.c`](drop_kern.c))
by dropping every frame immediately.

```
#include <linux/bpf.h>

#define SEC(NAME) __attribute__((section(NAME), used))

SEC("prog")
int xdp_filter(struct xdp_md *ctx)
{
    return XDP_DROP;  // drop frame immediately!
}

char __license[] SEC("license") = "GPL";
```

We can compile this programs in two stages,
using `clang` to generate LLVM IR,
and using `llc` to build the object file.

```
$ clang -O2 -g -Wall -target bpf -c -emit-llvm -o drop_kern.ll drop_kern.c
$ llc -march=bpf -filetype=obj -o drop_kern.o drop_kern.ll
```

We load the resulting object file into the XDP hook for the loopback device.

```
$ sudo ip -force link set dev lo xdp obj drop_kern.o
```

The `-force` flag automatically replaces any previously-installed program,
so we don't have to unload the program with `xdp off` first.

Once again, we use `ping` to test the program.

```
$ ping localhost
PING localhost(localhost (::1)) 56 data bytes
^C
--- localhost ping statistics ---
11 packets transmitted, 0 received, 100% packet loss, time 421ms
```

This time, of course, all the packets are dropped.

We can use the `bpftool` to list all of the eBPF programs currently installed.

```
$ sudo bpftool prog
...
14: xdp  tag 57cd311f2e27366b  gpl
	loaded_at 2020-11-12T18:31:57-0700  uid 0
	xlated 16B  not jited  memlock 4096B
```

We can also use `bpftool` to dump an already-loaded program.

```
$ sudo bpftool prog dump xlated id 14
   0: (b7) r0 = 1
   1: (95) exit
```

## Instrumentation for Debugging

Instrumentation is critical for debugging.
This program ([`print_kern.c`](print_kern.c))
demonstrates how to print to the kernel debug tracing log.

```
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
    return XDP_PASS;  // pass frame on to networking stack
}

char __license[] SEC("license") = "GPL";
```

This program includes several new features,
and defines a few macros to make them more convenient to use.
Notice, in particular, the "frame too small" check.
Without this check, the verifier will reject the program
because it can't prove the subsequent packet data access is safe.
This program prints the packet length and first 32 octets of data
to the kernel debug tracing log,
then passes the packet on to the networking stack.
Since `__u64` data is stored LSB-first,
we use `__builtin_bswap64()` to show the octets in network order.

Once the program is loaded,
we can simply use `cat` to watch the tracing log.

```
$ sudo cat /sys/kernel/debug/tracing/trace_pipe
```

## Quick Reference

Here are some commonly-used commands, collected for easy reference.

Compile eBPF/XDP program `kern.c`:

```
$ clang -O2 -g -Wall -target bpf -c kern.c -o kern.o
```

Dump `kern.o` source and assembly code:

```
$ llvm-objdump -S kern.o
```

List network devices:

```
$ ip link list
```

Install `kern.o` XDP filter on loopback device `lo`:

```
$ sudo ip link set dev lo xdp obj kern.o
```

Install XDP filter from section `prog`:

```
$ sudo ip link set dev lo xdp obj kern.o sec prog
```

Remove XDP filter:

```
$ sudo ip link set dev lo xdp off
```

Replace XDP filter:

```
$ sudo ip -force link set dev lo xdp obj kern.o
```

Display kernel trace messages (generated by `bpf_trace_printk`):

```
$ sudo cat /sys/kernel/debug/tracing/trace_pipe
```

Display information about eBPF programs currently installed:

```
$ sudo bpftool prog
```

List eBPF programs attached to network interfaces (xdp/tc/flow_dissector):

```
$ sudo bpftool net list
```

Display eBPF shared Map data for map id `42`:

```
$ sudo bpftool map dump id 42
```

Set map entry `0` to "Hello" in map id `42` (where key size is 4 and value size is 8):

```
$ sudo bpftool map update id 42 key 0 0 0 0 value 72 101 108 108 111 10 0 0
```

Set map entry `1` to `0xFF` in map id `42` (where key size is 4 and value size is 8):

```
$ sudo bpftool map update id 42 key 1 0 0 0 value 255 0 0 0 0 0 0 0
```

Disassemble installed eBPF program with id `13`:

```
$ sudo bpftool prog dump xlated id 13
```
