CLANG ?= clang

ARCH   ?= aarch64
TRIPLE := $(ARCH)-unknown-linux-gnu
OUT    := ../out/libuc/$(ARCH)
# arm64's uapi export predates the second architecture and keeps its name;
# x86_64's comes from `docker buildx bake uapi-x86_64`.
UAPI   := $(if $(filter x86_64,$(ARCH)),../out/uapi-x86_64/include,../out/uapi/include)

# Probed because Apple clang has no minimal runtime; the handlers the
# instrumentation calls are src/ubsan.c. local-bounds is extra to the
# undefined group: it lowers to a bare brk, needing no handler symbol.
UBSAN_WANT := -fsanitize=undefined,local-bounds -fsanitize-minimal-runtime -fno-sanitize-recover=all
UBSAN := $(shell $(CLANG) --target=$(TRIPLE) $(UBSAN_WANT) -x c -c /dev/null -o /dev/null 2>&1 \
                 | grep -q . || echo '$(UBSAN_WANT)')

CPPFLAGS := -nostdlibinc -isystem $(UAPI) -Iinclude
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

# Vendored code compiles with its upstream's expectations, not -Weverything;
# the contract is enforced where it is defined — at the link.
VENDOR_CFLAGS := --target=$(TRIPLE) -std=c17 -ffreestanding \
                 -fno-stack-protector -fno-omit-frame-pointer -g -O1 -Wall \
                 $(UBSAN) -isystem include -DLZ4_FREESTANDING=1 \
                 -DLZ4_memcpy=__builtin_memcpy -DLZ4_memset=__builtin_memset \
                 -DLZ4_memmove=__builtin_memmove
CFLAGS := --target=$(TRIPLE) -std=c23 -ffreestanding -fno-stack-protector \
          -fno-omit-frame-pointer -g -O1 $(WARN) -Werror $(UBSAN)
ASFLAGS := --target=$(TRIPLE) -ffreestanding -g
# Entry point and link contract both live in libuc.ld. -z norelro: RELRO
# only matters to a dynamic loader and this binary has no interpreter;
# with it, lld splits the RW image into two PT_LOAD segments sharing a
# page, and the kernel's BSS zero-fill wipes .init_array in the first.
LDFLAGS := --target=$(TRIPLE) -nostdlib -nostartfiles -static -fuse-ld=lld \
           -Wl,-T,libuc.ld -Wl,-z,norelro

CORE := $(OUT)/arch/$(ARCH)/start.o $(OUT)/start.o $(OUT)/ubsan.o \
        $(OUT)/string.o

OBJECTS     := $(CORE) $(OUT)/main.o
LZ4_OBJECTS := $(CORE) $(OUT)/vendor/lz4.o $(OUT)/lz4test.o

.PHONY: all check verify initramfs clean

all: $(OUT)/start.elf $(OUT)/lz4.elf

# clangd picks the nearest compile_commands.json walking up from each
# file, so libuc/ carries its own and the root one keeps src/.
check: compile_commands.json $(OBJECTS) $(LZ4_OBJECTS)

# Runs the probe, so it needs the aarch64 container; 23 is the pass status.
verify: all
	$(OUT)/start.elf; test $$? -eq 23
	$(OUT)/lz4.elf; test $$? -eq 23

$(OUT)/start.elf: $(OBJECTS) libuc.ld
	$(CLANG) $(LDFLAGS) -o $@ $(OBJECTS)

$(OUT)/lz4.elf: $(LZ4_OBJECTS) libuc.ld
	$(CLANG) $(LDFLAGS) -o $@ $(LZ4_OBJECTS)

$(OUT)/arch/$(ARCH)/start.o: src/arch/$(ARCH)/start.S Makefile
	@mkdir -p $(@D)
	$(CLANG) $(ASFLAGS) $(CPPFLAGS) -c $< -o $@

$(OUT)/start.o: src/start.c Makefile
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) $(IMPL_WARN) $(CPPFLAGS) -c $< -o $@

$(OUT)/ubsan.o: src/ubsan.c Makefile
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) $(UBSAN_WARN) $(CPPFLAGS) -c $< -o $@

# -fno-builtin: otherwise the loops idiom-match into calls to themselves.
$(OUT)/string.o: src/string.c Makefile
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) -fno-builtin $(CPPFLAGS) -c $< -o $@

$(OUT)/vendor/lz4.o: vendor/lz4/lz4.c Makefile
	@mkdir -p $(@D)
	$(CLANG) $(VENDOR_CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OUT)/lz4test.o: test/lz4.c Makefile
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) -isystem vendor/lz4 $(CPPFLAGS) -c $< -o $@

$(OUT)/main.o: test/main.c Makefile
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

compile_commands.json: Makefile $(wildcard src/*.c test/*.c)
	@printf '[' > $@
	@sep=""; for f in $(wildcard src/*.c test/*.c); do \
	  case $$f in \
	    src/ubsan.c)  extra="$(UBSAN_WARN)";; \
	    src/string.c) extra="-fno-builtin";; \
	    src/*)        extra="$(IMPL_WARN)";; \
	    test/lz4.c)   extra="-isystem vendor/lz4";; \
	    *)            extra="";; \
	  esac; \
	  printf '%s\n  {"directory": "%s", "file": "%s", "command": "%s"}' \
	    "$$sep" "$(CURDIR)" "$(CURDIR)/$$f" \
	    "$(CLANG) $(CFLAGS) $$extra $(CPPFLAGS) -c $$f" >> $@; \
	  sep=","; done
	@printf '\n]\n' >> $@

# Wraps an ELF as /init for the VM: INITRD=out/libuc/lz4.initramfs.cpio.gz
# ./run.sh. PID 1 exiting panics the kernel, and the panic line carries the
# status: exitcode=0x1700 is 23. Deliberately no prerequisite on the ELF —
# it links in the container, and the host, which cannot link, only packages
# whatever the container last built.
$(OUT)/%.initramfs.cpio.gz:
	@test -f $(OUT)/$*.elf || \
	  { echo "$(OUT)/$*.elf missing: run make verify in the container first"; exit 1; }
	@rm -rf $(OUT)/root-$* && mkdir -p $(OUT)/root-$*
	@cp $(OUT)/$*.elf $(OUT)/root-$*/init
	cd $(OUT)/root-$* && echo init | cpio -o -H newc | gzip > $(abspath $@)

initramfs: $(OUT)/start.initramfs.cpio.gz $(OUT)/lz4.initramfs.cpio.gz

clean:
	rm -rf $(OUT) compile_commands.json
