# What the end-to-end sketch taught

2026-08-29: the whole ticket arc — block create/destroy, install, fibers,
main on the root fiber — was built once as an uncommitted sketch to see the
shape. All three probes exit 0 on both architectures (containers, UBSan
armed). The code is a reference, not the product; these are the findings no
ticket captures.

## libuc.ld drops orphan sections where they break W^X

The script's permission grouping (R, RX, RW on MAXPAGESIZE boundaries) only
holds for sections it names. `.got` was not named, so LLD placed the orphan
directly after `.text`, unaligned: an 8-byte RW PT_LOAD sharing `.text`'s
last page. The kernel maps the page twice, RW last, execute is gone, and the
process takes an instruction abort on the *first fetch of `_start`*. The
first GOT entry arrived with the first writable global in a no-PT_TLS link —
the hazard was latent the whole time.

Fix: `.got`/`.got.plt` captured in the RW group. Worth doing beyond that:
`--orphan-handling=error` in the probe link args, so the next orphan is a
link error instead of a page-permission heisenbug.

Diagnosis path when a container binary dies with no debugger:
`docker run --privileged`, `echo 1 > /proc/sys/debug/exception-trace`, run
the binary, `dmesg` prints pc/lr/registers; map the pc with llvm-objdump.

## Toolchain traps

- `thread_local` is a C23 keyword. It cannot name a struct member; the fiber
  calls its block `thread_local_block`.
- `-Wpadded` is live in libuc's meson flags (the Make tree suppressed it).
  Structs either order fields densely or spell the tail padding out as a
  `reserved` array; implicit padding is a build failure.
- clang-format rewrites whitespace *inside* asm operands (`%%rbx` becomes
  `% % rbx`), and `#__VA_ARGS__` stringization preserves spacing, so the
  corruption lands in the emitted template — and a mangled template can
  still assemble into a wrong binary rather than fail the build. Every
  stringized-asm block gets `// clang-format off` fences.

## Measured ABI facts (this toolchain, disassembly + container runs)

- aarch64 variant 1: block at TP + round_up(16, p_align); observed TP+16 and
  TP+20 for p_align 4. sysvabi64 is the normative source (16-byte TCB, TP
  addresses it), not AAELF64.
- x86-64 variant 2: block ends alignTo(p_memsz, p_align) *below* TP with no
  clamping — a char-only executable resolves to `%fs:-0x1`. Taking any
  thread-local's address emits `movq %fs:0, %rax`, so the eight bytes at TP
  must hold TP's own value before the block is ever installed; that write
  belongs to creation (tcb_init), not installation. TP must additionally be
  pointer-aligned, which placement gets by padding *below* the block — block
  and TP move together and the linker's distance survives.
- The TCB size is ABI-fixed on aarch64 (16) and libuc's own choice on x86-64
  (8, the self-pointer): variant 2 keeps the TCB above TP where no compiled
  offset reaches, so it can grow without moving a block.

## Fiber-layer choices the sketch made; the real landing re-decides or confirms

- `current_fiber` is a single global — correct while there is one kernel
  thread; becomes per-kernel-thread state when threads arrive.
- Resume installs the target's block; the return path reinstalls the
  resumer's. The bootstrap context has no block (previous == nullptr, no
  install), which makes an invariant: bootstrap code must never touch a
  `_Thread_local`.
- Fiber stacks are plain RW anonymous mmaps: no guard page, caller-chosen
  size, no floor. Guard pages are .scratch/stacks.md territory.
- Trampoline ABI: the fiber pointer rides a callee-saved register (x19 /
  r12) through the switch. The x86 trampoline must `call` (not jump to) the
  C half so rsp is 8 mod 16 inside it.

## API decisions from the design discussion (not in any ticket)

- Block API shape: create/destroy owning their mmap was chosen while TLS
  preceded fibers in the queue; the queue inversion (fibers first) reopened
  it, and the decision now belongs to the block ticket, made against the
  fiber as the real caller. The alternative is the musl/glibc shape —
  geometry + carve into caller memory, allocation owned by the thread/fiber
  layer so stack and TLS share one mapping. Both libcs ended there; we get
  to arrive with evidence. The handle lives in a src/-private header either
  way, so nothing about the choice is ABI.
- The handle is the transparent four-field struct {mapping, length, block,
  thread_pointer}: write down what destroy, install, and the acceptance
  probe need rather than re-deriving any of it.
- Internal-symbol visibility for a shared libuc: deferred. When it lands,
  the lean is fail-closed — `-fvisibility=hidden` globally with the public
  surface marked default — over musl-style hidden annotations, because a
  forgotten export fails the link loudly while a forgotten annotation
  silently freezes an accidental ABI.

## Running acceptance without the VM

Static probes execute directly in containers: `docker run -v <builddir>:/p
alpine /p/<probe>.elf` — natively for aarch64, `--platform linux/amd64` for
the x86-64 build. Exit codes are the acceptance signal. This is the
cross/runnable.ini story realized without a matching host.
