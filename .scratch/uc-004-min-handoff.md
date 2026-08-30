# UC-004-min handoff

The staged path keeps the TCB while deferring compiler-visible TLS:

```text
UC-004-min: TCB-only block allocation/destruction
UC-005:     thread-pointer installation
UC-006-min: TCB/fiber binding during switches
UC-007:     suspension/resumption
later:      PT_TLS image and _Thread_local data
```

This preserves the important design—the TCB is present and fiber-owned—while
postponing only the static TLS payload.

UC-004-min now allocates an architecture-correct runtime block, initializes the
TCB self-pointer and null fiber pointer, supports PT_TLS image/zero-fill when
the executable declares one, and destroys independent blocks. It does not
install the thread pointer. An executable without `PT_TLS` still receives a
TCB-only block.

Next work is UC-005: install a block's recorded thread-pointer value without
allocating or changing block ownership. UC-006-min can then attach each block
to its fiber and carry the TCB through context switches, giving UC-007 the
current-fiber lookup it needs without requiring a compiler `_Thread_local`
variable yet.

Validation note: the changed UC-004-min translation units compile for both
AArch64 and x86-64. Full Meson linking remains blocked by the pre-existing
Clang-22 rejection of `__builtin_bswapg` in `src/string/memcmp.c`.
