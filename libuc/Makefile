CLANG ?= clang

TRIPLE := aarch64-unknown-linux-gnu
OUT    := ../out/libuc
UAPI   := ../out/uapi/include

# Same probe as the root Makefile: Apple clang has no minimal runtime, the
# container's clang does. The handlers the instrumentation calls are libuc's
# own (src/ubsan.c).
UBSAN_WANT := -fsanitize=undefined -fsanitize-minimal-runtime -fno-sanitize-recover=all
UBSAN := $(shell $(CLANG) --target=$(TRIPLE) $(UBSAN_WANT) -x c -c /dev/null -o /dev/null 2>&1 \
                 | grep -q . || echo '$(UBSAN_WANT)')

CPPFLAGS := -nostdlibinc -isystem $(UAPI)
# Two suppressions beyond the root set, both libuc-specific:
#   reserved-identifier      libuc is the implementation, so __libuc_* and
#                            __ubsan_* are exactly the names the reserved
#                            namespace exists to protect for this code.
#                            Per-file pragmas satisfied the compiler but
#                            not clangd: a diagnostic push that precedes
#                            the first declaration lands in the preamble,
#                            is lost on preamble replay, and the pop then
#                            errors in the editor on code the build accepts.
#   empty-translation-unit   fires on clangd's preamble for a file whose
#                            head is only comments and preprocessor lines
#                            (ubsan.c: a #define and its invocations) —
#                            the whole file is never empty.
WARN := -Weverything -Wno-pre-c23-compat -Wno-pre-c11-compat \
        -Wno-declaration-after-statement -Wno-c++98-compat \
        -Wno-c++98-compat-pedantic -Wno-c++-compat \
        -Wno-unsafe-buffer-usage -Wno-unused-command-line-argument \
        -Wno-reserved-identifier -Wno-empty-translation-unit
CFLAGS := --target=$(TRIPLE) -std=c23 -ffreestanding -fno-stack-protector \
          -fno-omit-frame-pointer -g -O1 $(WARN) -Werror $(UBSAN)
ASFLAGS := --target=$(TRIPLE) -ffreestanding -g
# The entry point comes from libuc.ld's ENTRY, and the script is also the
# enforcement of the link contract: legacy .init/.fini/.ctors/.dtors input
# fails the link there rather than being silently accepted.
#
# -z norelro because PT_GNU_RELRO is dead metadata in a binary with no
# interpreter — only a dynamic loader ever acts on it. Worse than dead:
# grouping RELRO sections splits the RW image into two PT_LOAD segments that
# share a page, and the kernel's zeroing of the second (BSS-only) segment
# wipes .init_array and .got in the first. One RW segment, no overlap.
LDFLAGS := --target=$(TRIPLE) -nostdlib -nostartfiles -static -fuse-ld=lld \
           -Wl,-T,libuc.ld -Wl,-z,norelro

OBJECTS := $(OUT)/arch/aarch64/start.o $(OUT)/start.o $(OUT)/ubsan.o \
           $(OUT)/main.o

.PHONY: all check verify clean

all: $(OUT)/start.elf

# Same contract as the root Makefile: the editor reads the flags the build
# uses, or clangd guesses — and a guess without -std=c23 reports bogus
# errors ([[noreturn]] mid-macro, unbalanced diagnostic push/pop) in code
# that compiles. clangd picks the nearest compile_commands.json walking up
# from each file, so this one covers libuc/ and the root one keeps src/.
check: compile_commands.json $(OBJECTS)

# check compiles; only this proves the link contract end to end, so it can
# only run where the ELF can execute (the aarch64 container). 23 is the
# probe's pass status — anything else is a startup defect.
verify: all
	$(OUT)/start.elf; test $$? -eq 23

$(OUT)/start.elf: $(OBJECTS) libuc.ld
	$(CLANG) $(LDFLAGS) -o $@ $(OBJECTS)

$(OUT)/arch/aarch64/start.o: src/arch/aarch64/start.S Makefile
	@mkdir -p $(@D)
	$(CLANG) $(ASFLAGS) $(CPPFLAGS) -c $< -o $@

$(OUT)/start.o: src/start.c Makefile
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OUT)/ubsan.o: src/ubsan.c Makefile
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OUT)/main.o: test/main.c Makefile
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

compile_commands.json: Makefile $(wildcard src/*.c test/*.c)
	@printf '[' > $@
	@sep=""; for f in $(wildcard src/*.c test/*.c); do \
	  printf '%s\n  {"directory": "%s", "file": "%s", "command": "%s"}' \
	    "$$sep" "$(CURDIR)" "$(CURDIR)/$$f" \
	    "$(CLANG) $(CFLAGS) $(CPPFLAGS) -c $$f" >> $@; \
	  sep=","; done
	@printf '\n]\n' >> $@

clean:
	rm -rf $(OUT) compile_commands.json
