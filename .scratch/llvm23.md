# LLVM 23 and C23 constexpr objects

## Context

The flake packages LLVM 23 RC3 (`llvm23-rc3.nix`, exposed as
`packages.aarch64-darwin.llvm23-rc3`), but the devshell still builds from
`llvmPackages_22` (`flake.nix:28`). The reason to flip is not general
freshness: clang 23 completes a C23 feature the codebase can actually use,
and clang 22's version of it is unusable under our warning policy.

C23 (N3018) makes `constexpr` objects of *structure* type real named
constants: member access is a constant expression, usable in `static_assert`
and array bounds. The two compilers differ exactly there:

- **clang 22.1.8** accepts the object definition, but member access in a
  constant context is only folded as a GNU extension —
  `-Wgnu-folding-constant`, which `-Weverything -Werror` turns fatal. Scalar
  constexpr (already used throughout `src/` and `libuc/`) is unaffected.
- **clang 23.1.0-rc3** implements the conforming semantics. Verified: a
  `constexpr struct` whose members feed a `static_assert` on a cross-field
  relationship and an array bound compiles clean under
  `-std=c23 -ffreestanding -Weverything -Werror`, including against the
  pinned uapi headers (`-isystem out/uapi/include -nostdlibinc`).

So constexpr structs gate on the devshell flip, and nothing else does.

## The boundary that decides where it applies

C23 constexpr pointers may only be initialized to `nullptr`, and file-scope
constexpr implies internal linkage (like `static`). That yields a clean rule:

**Configuration that names values can move to translation time; configuration
that names addresses cannot.**

- Value side: ring flags, strides, ports, sizes, magics — all eligible.
- Address side: anything holding a function pointer or buffer pointer
  (`src/crash.c`'s `sigaction`, `on_fault_ptr`, `stack_t`) — ineligible,
  permanently, by the standard's own rule.

## The non-obvious win: constexpr instances of kernel uapi structs

The habit everywhere is to build ABI structs at runtime because that is how
it has always been done. The ones that hold only values can instead be
finished artifacts in `.rodata` before boot — and asserting on the object
itself is stronger than asserting on a parallel scalar, because the check is
attached to the thing actually handed to the kernel.

Candidate sites, verified against the tree:

| Site | Change |
|---|---|
| `src/main.c:173-177` | Replace the five scalar socket constants with a `constexpr struct sockaddr_in` — `htons` at translation time (`(__be16)((port >> 8) \| (unsigned short)(port << 8))`), `INADDR_ANY` as `{0}`. Verified clean vs uapi headers on RC3. |
| `src/ring.c:11-16` | A `constexpr struct io_uring_params` template with `static_assert(!(template.flags & IORING_SETUP_SQPOLL))` — invariant 2 encoded against the object handed to `io_uring_setup`. Runtime copies it, since the kernel writes back into params. |
| `src/ring.c` stride math | Derive instead of defend: `constexpr struct { size_t sqe, cqe; }` computed from `RT_RING_FLAGS` (`SQE128 ? 128 : 64`, `CQE32 ? 32 : 16`). The current static_assert only proves `sizeof` is safe *today*; the derivation stays correct if the flags ever change. This is "assert the flags, not the size" upgraded to "derive from the flags". |
| `src/fiber.c:12-16` | Group `RT_STACK_SIZE` / `RT_STACK_MAGIC` and the power-of-two assert into one named object. Starts paying rent when guard size and alignment join them. |

Marginal, considered and skipped: the `offsetof` table feeding `rt_switch`'s
asm (`src/arch/aarch64/context.c`) — the operands plus the existing adjacency
asserts already say the same thing; a named struct adds indirection, not
information. `libuc/src/string/` needs nothing: scalar `block_width` is all
memcmp wants.

## Adoption sequence

1. Flip the devshell's `llvm` from `llvmPackages_22` to the `llvm23-rc3`
   package in `flake.nix`.
2. Re-run `make check` and the libuc meson build before changing any source —
   a compiler bump can surface new `-Weverything` warnings on its own, and
   those should be triaged separately from the constexpr work.
3. Apply the four conversions above; each is small, mechanical, and
   independently checkable (RT-005/RT-009 acceptance output must not change).

RC3 is a release candidate: if it misbehaves, the fallback is reverting the
one flake line, and the constexpr changes revert with it cleanly since they
are source-compatible rewrites of existing constants.

## What C23 did and did not take

For perspective when reading this later: C23 took constexpr *objects* but not
constexpr *functions* — compile-time computation must be expression-shaped
(ternaries over flags are fine; derived tables are not). That is the next
edge, presumably C2y. For a freestanding PID 1 with no init helpers, even the
objects-only version is outsized: every relationship that moves from "checked
by the boot console" to "checked by static_assert over a named constant" is a
regression the no-test-harness policy never has to catch.
