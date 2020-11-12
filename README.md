# eBPF

eBPF experiments

## What is eBPF?

eBPF (extended Berkley Packet Filter) is a [virtual machine-language](eBPF.md)
designed for safe execution within the Linux kernel.

It is:
  * a verifier with extensive code-path analysis
  * an in-kernel interpreter
  * a just-in-time compiler
  * a potential processor-offload mechanism

It is used in several places to provide runtime-configurable code-injection into the kernel.

### How do I create eBPF?

It is possible to create eBPF machine code (64-bit instructions) directly,
and inject them into the kernel via the `bpf` system call.
There are even macros for synthesizing instructions in userland C programs.

However, it is usually easier to specify eBPF programs in a higher-level language.
The most common is "C", using the LLVM compiler tools with a "bpf" machine target.

### How do I interact with eBPF programs?

eBPF programs are attached to various hooks in the kernel.
They are essentially event-handlers.
They are called from the kernel hook and given a context object specific to the hook type.
They return a result code whose meaning is defined by the hook type.
The eBPF verifier ensures that the code will always terminate and never access invalid memory.

The main way that eBPF code interacts with userland is via so-called "maps".
These are persistent data-structures with a key/value style interface to create/read/update/delete entries.
There are often both global and per-thread variations of these data-structures.
Information can be read/written in these "maps" from both userspace and the eBPF program.

In addition, some eBPF hooks allow the creation of various kernel events,
and those that are in the network packet-processing pipeline
can naturally pass packets through the network stack into userspace.

## What is XDP?

XDP (eXpress Data Path) is a specific kernel hook to which an eBPF program can be attached.
The XDP hook sits at the boundary between the device driver and the kernel's network stack.
A program attached to the XDP hook is called when the device receives a packet,
but before the kernel allocates resources (such as an `sk_buff`) or spends time parsing the data.
The eBPF/XDP program can read and/or write data in the packet.
The result code returned by the program determines if the packet will be
_dropped_, _redirected_, or _passed_ on to the network stack.

## How do I get started?

The experiments in this respository require an up-to-date Linux kernel running on a Raspberry Pi 3 or 4.
You will have to compile your own kernel in order to enable XDP functionality.
The kernel source tree also contains tools and header files on which this respository depends.
Instructions for setup can be found in [`setup.md`](setup.md)
