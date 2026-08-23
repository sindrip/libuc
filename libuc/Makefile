CLANG ?= clang

TRIPLE := aarch64-unknown-linux-gnu
OUT    := ../out/libuc
UAPI   := ../out/uapi/include

CPPFLAGS := -nostdlibinc -isystem $(UAPI)
WARN := -Weverything -Wno-pre-c23-compat -Wno-pre-c11-compat \
        -Wno-declaration-after-statement -Wno-c++98-compat \
        -Wno-c++98-compat-pedantic -Wno-c++-compat \
        -Wno-unsafe-buffer-usage -Wno-unused-command-line-argument
CFLAGS := --target=$(TRIPLE) -std=c23 -ffreestanding -fno-stack-protector \
          -fno-omit-frame-pointer -g -O1 $(WARN) -Werror
ASFLAGS := --target=$(TRIPLE) -ffreestanding -g
# The entry point comes from libuc.ld's ENTRY, and the script is also the
# enforcement of the link contract: legacy .init/.fini/.ctors/.dtors input
# fails the link there rather than being silently accepted.
LDFLAGS := --target=$(TRIPLE) -nostdlib -nostartfiles -static -fuse-ld=lld \
           -Wl,-T,libuc.ld

OBJECTS := $(OUT)/arch/aarch64/start.o $(OUT)/start.o $(OUT)/main.o

.PHONY: all check clean

all: $(OUT)/start.elf

check: $(OBJECTS)

$(OUT)/start.elf: $(OBJECTS) libuc.ld
	$(CLANG) $(LDFLAGS) -o $@ $(OBJECTS)

$(OUT)/arch/aarch64/start.o: src/arch/aarch64/start.S Makefile
	@mkdir -p $(@D)
	$(CLANG) $(ASFLAGS) $(CPPFLAGS) -c $< -o $@

$(OUT)/start.o: src/start.c Makefile
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OUT)/main.o: test/main.c Makefile
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf $(OUT)
