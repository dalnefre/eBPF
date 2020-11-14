# eBPF Virtual Machine

eBPF (extended Berkley Packet Filter) is a virtual machine-language designed for safe execution within the Linux kernel.

Programs written in eBPF can be attached to various _hooks_ in the kernel.
When certain events occur, the kernel will call the program attached to the hook,
passing hook-specific context data.
The value returned by the program can affect the control-flow of the kernel
in hook-specific ways.
For example,
an eBPF program attached to the `XDP` hook
gets read/write access to raw packet data from a device as it's _context_;
and can cause the packet to be dropped, redirected, or passed on to the networking stack
based on the return value of the program.

## eBPF Execution Environment

While eBPF code executes in the kernel (thus with kernel permissions),
the virtual machine-language instructions and restricted address space (along with the verifier)
limit the execution environment to ensure safety and security.

### Data

The data available to an eBPF program is carefully controlled.

```
     Registers (64-bit)             Memory                   Maps (shared)
    +-----------------------+                               +------------+
 r0 | return value          |      +-----------------+      | key: value |
 r1 | first arg / context   |----->| hook-specific   |      :    :       :
 r2 | second arg            |      : context data    :      +------------+
 r3 | third arg             |      +-----------------+
 r4 | fourth arg            |                                     .
 r5 | fifth arg             |
 r6 | first saved data      |                                     .
 r7 | second saved data     |      +-----------------+
 r8 | third saved data      |      | 512B temporary  |            .
 r9 | fourth saved data     |      : working memory  :
r10 | stack ptr (read-only) |----->+-----------------+
    +-----------------------+
```

There are 11 registers, in which most operations are performed.
On entry to the program, `r1` contains a pointer to hook-specific context data.
The read-only `r10` points to the end of a 512 byte "scratch space",
used to spill/fill registers and hold other temporary data.

Note that there is no _data segment_ in an eBPF program.
Constant/literal data must be encoded in the _immediate_ fields of various instructions.
Mutable state is not preserved between executions.
The working memory begins empty for each invocation of the program.

Maps provide a persistent mutable key/value store,
which may be shared between programs.
Maps are accessable from userland,
and may be _pinned_ so that they persist beyond the lifetime of a program.

### Code

The code of an eBPF program is sandboxed and verified for safety.

```
     Program                        Kernel                   Shared Prog.
    +-----------------------+                         
    | 64-bit instructions   |      +-----------------+
    | including BPF_CALL    |----->| opaque helper   |
    :                       :      : procedures      :      +------------+
    | (4096 inst. limit)    |      | incl. TAIL_CALL |----->| more inst. |
    +-----------------------+      +-----------------+      :            :
```

All eBPF programs are event-driven.
They are called from the kernel
when specific events occur, based on the hook-type.
Some hooks allow programs to be _off-loaded_ for execution directly in supported devices.
The verifier ensures that the code executes in a small finite amount of time,
and only access valid memory locations.

There are quite a few helper procedures (also hook-specific)
which perform operations that can not be safely expressed in eBPF.
These are hard-coded into the kernel.
Helpers are invoked with the `BPF_CALL` instruction,
which passes arguments in `r1`-`r5` and receives a return value in `r0`.
One of the helpers performs a `TAIL_CALL`,
which allows a limited form of chaining
to persistently-registered eBPF programs.

## Learn More

The folks at Cilium have provided [excellent in-depth documentation](https://docs.cilium.io/en/latest/bpf/), if you would like to learn more.
