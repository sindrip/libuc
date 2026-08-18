# BPF directions

Status: **ideation, 2026-08-18; partially verified against `out/src/` same
day** (RESTRICTIONS, sched_ext config, IO_URING_BPF config — cites inline).
Everything not carrying a cite must still be confirmed in the pinned tree —
file and line, per AGENTS.md — before it becomes a ticket.

Related: plan.md lists the in-kernel BPF loop (`io_uring/loop.c`,
`io_uring/bpf-ops.c`) as "not now, but stay reachable", and Clang was chosen as
the runtime compiler partly to keep it reachable.

**Invariant 1 amendment required before any of this ships**: `bpf(2)` (and for
direction 1, `seccomp(2)`/`prctl(2)`) have no ring opcodes, so they belong on
invariant 1's enumerated direct-syscall list — a list amendment, not a
purity-exception registry entry (the registry is for opcode-exists-but-
bypassed cases like `raw_write`). AGENTS.md edit, pending discussion.

## Why BPF is unusually cheap for this project

BPF's dominant cost everywhere else is kernel portability: CO-RE relocations,
version skew, verifier behaviour drifting release to release. The pinned kernel
deletes all of it. Programs compile against the exact BTF of the exact tree in
`out/src/`, forever. BPF here is BPF with its worst problem removed.

## Directions, ranked by how much they change

### 1. Compile the purity invariant into the kernel

Invariant 1 and the exception registry are prose enforced by review. Generate a
seccomp (or BPF-LSM) policy *from the registry*: the direct-syscall allowlist
becomes the only permitted syscall numbers; anything else kills the process. A
purity violation stops being a review miss and becomes a crash with a
backtrace. Under libuc with vendored C code this doubles as the security story:
guest code physically cannot bypass the ring.

seccomp only closes half the boundary: it polices *syscalls*, and a
compromised guest still has arbitrary I/O through the ring. The ring-side
analog is `IORING_REGISTER_RESTRICTIONS` — an opcode allowlist registered at
ring setup. The pair is what makes "invariants as installed policy" actually
seal: seccomp pins the syscall surface to the invariant-1 list, RESTRICTIONS
pins the ring surface to the opcodes the runtime actually uses.

RESTRICTIONS verified on 7.2 (`out/src/io_uring/register.c:118-165`): SQE
opcode allowlist (`IORING_RESTRICTION_SQE_OP`), register-op allowlist, and
SQE-flags allowed/required masks. Registration is only accepted while the
ring is still `IORING_SETUP_R_DISABLED` (`register.c:173`), so the sealing
sequence is: setup with `R_DISABLED` → register restrictions → enable rings.
Note the interaction recorded in transport.md: MSG_RING to a still-disabled
ring returns `-EBADFD`, so enable before any cross-core doorbell.

Verify remaining: seccomp filter interaction with `io_uring_enter`; whether
the syscall side should be seccomp (cheap, number granularity) or BPF-LSM
(richer, more moving parts). Start with seccomp — the registry is literally a
number list.

### 2. `bpf fn` — kernel offload as a language dialect

The future language already needs a `bare fn` dialect (no alloc, no suspend) so
the runtime can be written in it. A `bpf fn` dialect is the same move aimed at
the verifier: bounded loops, no alloc, restricted calls — and the compiler
emits BPF bytecode. "Wake my fiber only when the payload matches X" compiles to
a socket filter or a hook in the in-kernel loop, attached by the runtime
automatically. Rust and Zig can emit BPF but have zero integration with their
runtimes' event loops; a language whose effect system spans user/`bare`/`bpf`
and auto-splits programs across the boundary is unclaimed territory. Biggest
single idea in this file; furthest out (needs the language to exist).

### 3. XDP flow steering — shared-nothing enforced at the NIC

An XDP program hashes flows to cores so a connection's packets always arrive
where its task lives: no cross-core socket state, softirq work lands on the
owning core. A BPF arena/map that userspace updates with per-core load lets
*new* connections steer to the least-loaded ring — work distribution without
work stealing, upholding invariant 3 instead of negotiating with it.
Needs virtio-net (milestone 2) and multi-core (milestone 3) first.

Verify: XDP on virtio-net in this config; arena availability on 7.2.

### 4. sched_ext — the runtime ships its own CPU scheduler

As PID 1 the runtime owns the machine, so it can install a BPF scheduler whose
whole policy is: worker threads own their cores; every kthread, softirq, and
io-wq stray is shepherded to core 0. Replaces `isolcpus` boot-arg folklore with
policy-as-code the runtime installs about itself. This is the answer to the
threat RT-008 detects, not just a better tripwire.

Verified: `CONFIG_SCHED_CLASS_EXT is not set` in `out/kernel.config` — as
predicted, this direction costs a fragment addition and a kernel rebuild
before anything can be tried. Still to verify: minimal scheduler size.
(Bonus finds while checking: `CONFIG_IO_URING_BPF=y` and
`CONFIG_IO_URING_BPF_OPS=y` are already resolved on — the in-kernel loop
needs no config work — and `CONFIG_IO_URING_ZCRX=y` (zero-copy receive) is
on, adjacent to direction 3's steering ideas.)

### 5. Kernel-resident watchdog

The planned watchdog (core *i* watches core *i+1* via `IORING_OP_TIMEOUT` and
relaxed ticks) requires the watcher's userspace to be alive. A BPF timer in the
kernel checking per-core heartbeat words detects a wedged core even when every
userspace thread is spinning, and can dump state from kernel context. Strictly
stronger; still no extra thread, no extra core.

Verify: `bpf_timer` and map sharing shape on 7.2; what a BPF program may do on
detection (trace? console? reboot needs thought).

### 6. RT-008 becomes precise: fentry on the io-wq enqueue path

Today's tripwire is polling `/proc/self/task` for `iou-wrk-*`. A fentry program
on the io-wq enqueue function fires at the moment of the first punt, carrying
the opcode that caused it. The tripwire becomes an alarm with a culprit.

Verify: the enqueue symbol in `out/src/io_uring/io-wq.c`; fentry availability
under this config.

### 7. The observability plane

The VM has no shell, no perf, no ptrace tooling. BPF on io_uring's tracepoints
gives uringscope-class introspection of submission/completion behaviour from
inside; perf_event BPF sampling gives self-profiling of PID 1. When manual
console inspection stops scaling (Testing section names the revisit
conditions), this is where the tooling comes from — not from reintroducing a
userland.

### 8. In-kernel loop — promoted to `.scratch/bpf-loop.md`

The interface has been read and verified (loop.c, bpf-ops.c, register.c) and
the reap-loop lowering mapped in detail, including a program sketch, the
user_data encoding change it wants, and the real cost (a freestanding
struct_ops loader). See bpf-loop.md.

### 9. sockmap/sk_msg splice — the proxy fast path

If a proxy is ever the demo application: sockmap redirection forwards payload
socket-to-socket entirely in-kernel, tasks touch only control decisions.
Composes with the ring rather than competing with it. Parked until an
application exists.

## The meta-observation

Items 1, 3, 4, 5 share a shape: **invariants stop being documentation and
become installed kernel policy.** A hosted runtime cannot seal the kernel
around itself; a portable one cannot afford to. This position can do both, and
it is the same move this repo already makes with documents — the registry
compiling to an enforced policy is AGENTS.md compiling to enforcement.

## Rules for promoting anything above to a ticket

1. Read the relevant `out/src/` code and cite file:line in the ticket.
2. Confirm the config actually enables it — `docker buildx bake config`, then
   read `out/kernel.config`, not the fragment.
3. One direction per ticket, with a mechanically checkable acceptance string,
   same as everything else.
