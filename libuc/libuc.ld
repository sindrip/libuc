/*
 * libuc's link contract: one startup object (arch/<arch>/start.S), one
 * initializer mechanism (.init_array, walked by start.c between the two
 * boundary symbols defined here), and nothing inherited from the historical
 * crt protocol — no crti/crtn, no crtbegin/crtend.
 *
 * The .libuc.reject.* output sections exist only to be measured: they
 * collect every section a legacy or not-yet-supported startup scheme would
 * use, and the ASSERTs require them empty. An object built by a
 * pre-.init_array toolchain fails the link with a targeted message instead
 * of silently never initializing. This must stay explicit — the linker's
 * built-in rules would otherwise accept and sort the legacy sections,
 * granting exactly the compatibility this contract refuses.
 */

ENTRY(_start)

SECTIONS
{
  . = 0x400000 + SIZEOF_HEADERS;

  .text   : { *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }

  . = ALIGN(CONSTANT(MAXPAGESIZE));

  .init_array : {
    PROVIDE_HIDDEN(libuc_init_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    PROVIDE_HIDDEN(libuc_init_array_end = .);
  }

  .data : { *(.data .data.*) }
  .bss  : { *(.bss .bss.*) *(COMMON) }

  /* Collected only to be measured; KEEP so --gc-sections cannot hide a
   * violation. */
  .libuc.reject.init    : { KEEP(*(.init .init.*)) }
  .libuc.reject.fini    : { KEEP(*(.fini .fini.*)) }
  .libuc.reject.ctors   : { KEEP(*(.ctors .ctors.*)) }
  .libuc.reject.dtors   : { KEEP(*(.dtors .dtors.*)) }
  .libuc.reject.preinit : { KEEP(*(.preinit_array .preinit_array.*)) }
  .libuc.reject.finiarr : { KEEP(*(.fini_array .fini_array.*)) }
}

/* ENTRY alone is advisory: a link missing the startup object gets only a
 * "cannot find entry symbol" warning and an entry address of garbage. Make
 * it fatal. */
ASSERT(DEFINED(_start),
       "libuc: the startup object (arch/<arch>/start.o) is missing from the link")

/* Permanent: these formats died with the sandwich linking scheme. */
ASSERT(SIZEOF(.libuc.reject.init) == 0,
       "libuc: legacy .init code fragments are unsupported (crti/crtn-era object?)")
ASSERT(SIZEOF(.libuc.reject.fini) == 0,
       "libuc: legacy .fini code fragments are unsupported (crti/crtn-era object?)")
ASSERT(SIZEOF(.libuc.reject.ctors) == 0,
       "libuc: legacy .ctors pointer lists are unsupported (pre-.init_array toolchain?)")
ASSERT(SIZEOF(.libuc.reject.dtors) == 0,
       "libuc: legacy .dtors pointer lists are unsupported (pre-.init_array toolchain?)")

/* Phase discipline, not policy: supported the day something needs them. */
ASSERT(SIZEOF(.libuc.reject.preinit) == 0,
       "libuc: .preinit_array is unsupported until something needs it")
ASSERT(SIZEOF(.libuc.reject.finiarr) == 0,
       "libuc: .fini_array is unsupported until libuc has an exit path to run it")
