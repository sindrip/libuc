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
WARN := -Weverything -Wno-pre-c23-compat -Wno-pre-c11-compat \
        -Wno-declaration-after-statement -Wno-c++98-compat \
        -Wno-c++98-compat-pedantic -Wno-c++-compat \
        -Wno-unsafe-buffer-usage -Wno-unused-command-line-argument
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

check: $(OBJECTS)

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

clean:
	rm -rf $(OUT)
