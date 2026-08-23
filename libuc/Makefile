CLANG ?= clang

TRIPLE := aarch64-unknown-linux-gnu
OUT    := ../out/libuc
UAPI   := ../out/uapi/include

# Probed because Apple clang has no minimal runtime; the handlers the
# instrumentation calls are src/ubsan.c. local-bounds is extra to the
# undefined group: it lowers to a bare brk, needing no handler symbol.
UBSAN_WANT := -fsanitize=undefined,local-bounds -fsanitize-minimal-runtime -fno-sanitize-recover=all
UBSAN := $(shell $(CLANG) --target=$(TRIPLE) $(UBSAN_WANT) -x c -c /dev/null -o /dev/null 2>&1 \
                 | grep -q . || echo '$(UBSAN_WANT)')

CPPFLAGS := -nostdlibinc -isystem $(UAPI)
WARN := -Weverything -Wno-pre-c23-compat -Wno-pre-c11-compat \
        -Wno-declaration-after-statement -Wno-c++98-compat \
        -Wno-c++98-compat-pedantic -Wno-c++-compat \
        -Wno-unsafe-buffer-usage -Wno-unused-command-line-argument

# Implementation TUs define __libuc_*/__ubsan_* — the names the reserved
# namespace exists to protect for a libc's internals. test/main.c stands
# for application code and keeps the full set. empty-translation-unit:
# clangd compiles a file's pre-declaration head as a standalone preamble,
# and ubsan.c's head is comments and one #define.
IMPL_WARN  := -Wno-reserved-identifier
UBSAN_WARN := $(IMPL_WARN) -Wno-empty-translation-unit
CFLAGS := --target=$(TRIPLE) -std=c23 -ffreestanding -fno-stack-protector \
          -fno-omit-frame-pointer -g -O1 $(WARN) -Werror $(UBSAN)
ASFLAGS := --target=$(TRIPLE) -ffreestanding -g
# Entry point and link contract both live in libuc.ld. -z norelro: RELRO
# only matters to a dynamic loader and this binary has no interpreter;
# with it, lld splits the RW image into two PT_LOAD segments sharing a
# page, and the kernel's BSS zero-fill wipes .init_array in the first.
LDFLAGS := --target=$(TRIPLE) -nostdlib -nostartfiles -static -fuse-ld=lld \
           -Wl,-T,libuc.ld -Wl,-z,norelro

OBJECTS := $(OUT)/arch/aarch64/start.o $(OUT)/start.o $(OUT)/ubsan.o \
           $(OUT)/main.o

.PHONY: all check verify clean

all: $(OUT)/start.elf

# clangd picks the nearest compile_commands.json walking up from each
# file, so libuc/ carries its own and the root one keeps src/.
check: compile_commands.json $(OBJECTS)

# Runs the probe, so it needs the aarch64 container; 23 is the pass status.
verify: all
	$(OUT)/start.elf; test $$? -eq 23

$(OUT)/start.elf: $(OBJECTS) libuc.ld
	$(CLANG) $(LDFLAGS) -o $@ $(OBJECTS)

$(OUT)/arch/aarch64/start.o: src/arch/aarch64/start.S Makefile
	@mkdir -p $(@D)
	$(CLANG) $(ASFLAGS) $(CPPFLAGS) -c $< -o $@

$(OUT)/start.o: src/start.c Makefile
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) $(IMPL_WARN) $(CPPFLAGS) -c $< -o $@

$(OUT)/ubsan.o: src/ubsan.c Makefile
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) $(UBSAN_WARN) $(CPPFLAGS) -c $< -o $@

$(OUT)/main.o: test/main.c Makefile
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

compile_commands.json: Makefile $(wildcard src/*.c test/*.c)
	@printf '[' > $@
	@sep=""; for f in $(wildcard src/*.c test/*.c); do \
	  case $$f in \
	    src/ubsan.c) extra="$(UBSAN_WARN)";; \
	    src/*)       extra="$(IMPL_WARN)";; \
	    *)           extra="";; \
	  esac; \
	  printf '%s\n  {"directory": "%s", "file": "%s", "command": "%s"}' \
	    "$$sep" "$(CURDIR)" "$(CURDIR)/$$f" \
	    "$(CLANG) $(CFLAGS) $$extra $(CPPFLAGS) -c $$f" >> $@; \
	  sep=","; done
	@printf '\n]\n' >> $@

clean:
	rm -rf $(OUT) compile_commands.json
