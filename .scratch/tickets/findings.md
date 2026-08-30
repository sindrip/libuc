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

- The sketch's `current_fiber` was a single global — correct only while there
  was one kernel thread. The landing rejects it: UC-004 reserves a current-
  fiber word in each TCB, and UC-006 binds and reads it through the installed
  thread pointer. That scales to scheduler threads without shared mutable
  lookup state or a libuc `_Thread_local` symbol that would force `PT_TLS`.
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

  2026-08-30, deliberately still open: UC-006 builds on the committed
  create/destroy surface as a provisional owner, and the real decision moves
  to UC-011, made against measured spawn and recycle
  numbers rather than the projections in `../stacks.md` (per spawn, carve
  saves one mmap and its mmap_lock hit, plus one VMA per fiber; create-owns
  keeps the TCB fault-isolated in its own mapping and the stack scheme
  orthogonal). What keeps the swap an internal refactor in either direction:
  fiber code reaches the block only through the handle and the thread
  pointer, never through layout arithmetic; `thread_local_place` stays the
  sole geometry authority; `block_create` stays a thin wrapper over it.
- The handle is the transparent four-field struct {mapping, length, block,
  thread_pointer}: write down what destroy, install, and the acceptance
  probe need rather than re-deriving any of it.
- UC-009 ready queue (2026-08-30): intrusive link in the fiber, chosen for
  now over a caller-sized array. The decisive point: YIELD requeues from
  inside the loop, where failure has no sane answer — the link makes requeue
  infallible, the array makes it a capacity crash. Costs accepted: fiber.h
  carries a scheduler-owned field; one link means one list, so a future
  state not exclusive with readiness buys a second field; double-enqueue is
  an unchecked contract violation (silent self-loop) — accepted for now,
  to be made illegal by construction rather than trapped. Revisit when a
  non-exclusive state appears or the constructive fix lands.
- UC-009 dispatch (2026-08-30): the loop reuses `__libuc_fiber_resume`
  per turn. The documented alternative is a scheduler-owned switch path
  skipping resume's per-call here-context and TP save/restore — faster in
  principle, duplicates the protocol; adopt only if dispatch shows up in a
  measurement.
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

## 2026-08-30: review findings on the landed reactor

Two correctness findings arrived by review the day UC-013 landed; both
verified against `out/src/` before fixing.

- **Short positive submissions lost SQEs.** `io_submit_sqes` breaks on
  request-allocation failure with a positive short count
  (`io_uring.c:2046, 2064-2068`); `SUBMIT_ALL` rides over per-SQE prep
  errors only (`io_uring.c:2053-2059`). `__libuc_ring_submit` treated any
  non-negative return as full consumption and zeroed the batch — the tail
  SQEs vanished and their fibers waited forever. Fixed in the ring: on a
  short count the remainder memmoves to slot zero (SQ_REWIND rereads from
  there each enter, `io_uring.c:1969-1970`) and the enter retries; a short
  that submitted nothing returns -EAGAIN, never zero, so each pass makes
  progress. Waiting twice is harmless — the CQ wait is level-triggered.
- **The unbounded sweep starved I/O.** Known and ticketed as UC-015, but
  review sharpened it: under `DEFER_TASKRUN`, completions post only when
  task work runs at enter, so a yield storm starves even already-submitted
  operations — the NOP framing undersold it. UC-015 was pulled forward and
  landed with the simple policy (one enter per generation while anything
  is parked). Its counters churned twice in review conversation — in_flight
  gave way to a live/queued pair, which read as arithmetic — and settled as
  the structural pair `ready`/`parked`: name the places fibers stand, and
  every branch tests its own counter. The pair sits where the padding slot
  was, and UC-013's acceptance order moved to `A0 B0 B1 A1 B2 A2`.

- **Multi-CQE operations reviewed as a follow-up P1; assessed as a
  contract without a tripwire.** The mechanics are real — multishot accept
  posts `IORING_CQE_F_MORE` streams (`net.c:1699`), and the wake path
  treats every CQE as a wake — but nothing in the tree can form such an
  SQE: probes are NOP-only and no wrapper requests multishot. The surface
  cannot *prevent* one either, so the reap loop now traps on
  `F_MORE | F_NOTIF | F_SKIP` until UC-016 designs the operation record
  (an operation slab bpf-loop.md's offset encoding addresses; it
  references task-header slots, it is not them). `F_SKIP` is the
  CQE32/CQE_MIXED gap filler with `user_data = 0`
  (`io_uring.c:708-720`), unreachable on this ring config; if it appears,
  the config drifted.

- **The stopped background review's five candidates, re-verified and
  landed.** (1) SQE-flags passthrough: `park` copied caller flags
  verbatim, so `IOSQE_CQE_SKIP_SUCCESS` would silently drop the wake and
  `IOSQE_IO_LINK` would chain unrelated fibers' SQEs in one batch; park
  now traps on any flag. (2) `-EINTR` from enter now retries in
  `ring_submit` instead of trapping the scheduler. (3) The submit-retry
  progress claim is config-derived, now stated: NO_SQARRAY removes the
  only zero-with-success path (`io_get_sqe` cannot fail without an
  sq_array). (4) enter masks the wait leg's error behind any positive
  submit count (`io_uring.c:2689-2701`), including `-EBADR` for dropped
  CQEs; rather than design drop recovery, park bounds `parked` at
  `cq_entries`, which makes overflow — and therefore drops and the
  masking — unreachable until a ticket lifts the bound deliberately.
  (5) `__libuc_fiber_await` now documents the same no-current-fiber
  fault contract as yield.
