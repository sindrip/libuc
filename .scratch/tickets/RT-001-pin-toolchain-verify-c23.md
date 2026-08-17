---
id: RT-001
title: Pin Alpine toolchain by digest, verify C23
status: done
depends: []
---

## Goal

Decide, once and reproducibly, which C dialect this project actually compiles
as, and on which compiler. Everything downstream assumed C23 on an unpinned
toolchain.

## Outcome

**Pinning: done, as specced.** The digest sits on the `FROM` line in
`build/kernel.Dockerfile`. Routing it in from `docker-bake.hcl` was tried both
ways and rejected: neither a named build context nor a global `ARG` can be
declared once, so a pin belonging to a single stage ends up restated on every
target that reaches it — and the context form additionally leaves a bare
`docker build` silently unpinned.

**Toolchain: Clang for the runtime, GCC for the kernel.** Rationale in
`AGENTS.md`; it was not decided on C23 grounds.

Measured on the pinned image (alpine 3.24.1):

| | GCC 15.2.0 | Clang 22.1.3 |
|---|---|---|
| `-std=c23` | ✅ | ✅ |
| `constexpr`, `auto`, `nullptr`, `typeof` | ✅ | ✅ |
| `[[nodiscard]]`, `0b` literals, keyword `static_assert` | ✅ | ✅ |
| `<stdatomic.h>`, `<stdckdint.h>`, `<stdint.h>`, `<stdarg.h>` | ✅ | ✅ |
| `<stdbit.h>` | ❌ | ❌ |
| freestanding `-nostdlib -static` link | ✅ | ✅ |
| `-fsanitize-minimal-runtime` | ❌ | ✅ |

Both comfortably clear the C23 bar this project needs, so C23 did not decide the
toolchain. `-fsanitize-minimal-runtime` did, alongside BPF and lldb.

## `<stdbit.h>` — settled, do not revisit

Unavailable, and not fixable by changing compiler or base image. C23 lists
`<stdbit.h>` as freestanding-**required** and `<stdckdint.h>` as not — yet every
toolchain implements `<stdbit.h>` as a **libc** header (glibc 2.39+) and
`<stdckdint.h>` as a **compiler** header. Since this project links no libc, the
freestanding-required one is the unreachable one.

Verified absent from GCC 15.2, Clang 22.1.3, musl, and Apple clang 21. This is a
toolchain conformance gap, not a configuration mistake —
[LLVM #62248](https://github.com/llvm/llvm-project/issues/62248) asks precisely
who is supposed to own these headers.

**Consequence:** ring sizing uses intrinsics, which need no header and exist on
both compilers.

```c
static inline unsigned long long bit_ceil(unsigned long long x)
{ return x <= 1 ? 1 : 1ull << (64 - __builtin_clzll(x - 1)); }
```

Likewise `__builtin_ctzll`, `__builtin_popcountll`, `__builtin_add_overflow`.

## Acceptance — all met

- Pinned image compiler versions recorded above; both ≥ the C23 threshold.
- C23 core features compile under `-std=c23 -ffreestanding` on both.
- `<stdbit.h>` status recorded as MISSING with the reason, not assumed.
- Alpine pinned by digest on the `FROM` line; `docker buildx bake kernel` succeeds.

## Notes

RT-003 must add `clang` and `lld` to the `toolchain` stage's apk list. GCC stays
for the kernel build — both compilers live in the same image.

Reproduce any of the above with:

```sh
docker run --rm alpine sh -c 'apk add -q clang lld && clang --version | head -1
  printf "#include <stdbit.h>\nint main(void){return 0;}\n" > /b.c
  clang -std=c23 -ffreestanding -fsyntax-only /b.c && echo OK || echo MISSING'
```

Note this uses `alpine:latest`, not the pinned digest — fine for probing, but
never for a conclusion about what the build actually uses.
