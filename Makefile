BAKE   := build/kernel.Dockerfile docker-bake.hcl
UAPI   := out/uapi/include

CLANG  ?= clang
TRIPLE := aarch64-unknown-linux-gnu

UBSAN_WANT := -fsanitize=undefined -fsanitize-minimal-runtime -fno-sanitize-recover=all
# Apple clang has no minimal runtime; Alpine's clang 22 does. Probe rather than
# emit a warning on every local compile — the container build gets the real thing.
# Apple clang warns and exits 0, so test for silence rather than success.
UBSAN := $(shell $(CLANG) $(UBSAN_WANT) -x c -c /dev/null -o /dev/null 2>&1 \
                 | grep -q . || echo '$(UBSAN_WANT)')

# -nostdlibinc drops the platform's libc include paths but keeps clang's own,
# so <stdint.h> and <stdatomic.h> survive while <stdio.h> cannot be reached.
# Invariant 4 enforced by the compiler instead of by discipline.
#
# -isystem, not -I: the uapi headers are the kernel's own code and do not
# compile under -pedantic. <linux/io_uring.h> alone trips -Wzero-length-array
# on the SQE's cmd[0], and -Wgnu-empty-struct plus -Wflexible-array-extensions
# on __DECLARE_FLEX_ARRAY. Warnings are suppressed inside -isystem paths, which
# keeps -pedantic pointed at our code where it belongs. Relaxing the flag
# instead would silence those checks everywhere.
CPPFLAGS := -nostdlibinc -isystem $(UAPI)
# -Werror matches the container. Without it `make check` is green on code the
# real build rejects, and the diagnostic arrives from a docker log after a
# kernel rebuild instead of from the compiler in half a second.
CFLAGS   := -std=c23 -ffreestanding -fno-stack-protector -fno-omit-frame-pointer \
            -Wall -Wextra -pedantic -Wsign-conversion -Werror -g -O1 $(UBSAN)
# No -no-pie: -static already implies it (verified, ELF Type=EXEC either way),
# and clang errors under -Werror that the flag went unused.
LDFLAGS  := -nostdlib -nostartfiles -static -fuse-ld=lld

OBJ := $(patsubst src/%.c,out/obj/%.o,$(wildcard src/*.c)) \
       $(patsubst src/%.S,out/obj/%.o,$(wildcard src/*.S))

.PHONY: help kernel config uapi src run debug check tidy clean distclean

help:
	@awk -F: '/^[a-z][a-z-]*:/{print $$1}' $(MAKEFILE_LIST) | sort -u

kernel: out/vmlinuz
uapi:   $(UAPI)

# The kernel target also produces initramfs.cpio.gz from src/, so a source edit
# must invalidate it. Grouped targets (&:) need make 4.3; macOS ships 3.81, and
# both artefacts come from the same bake, so vmlinuz standing in for both is
# accurate rather than merely convenient.
out/vmlinuz: $(BAKE) build/kernel.config $(wildcard src/*)
	docker buildx bake kernel

$(UAPI): $(BAKE)
	docker buildx bake uapi

config:
	docker buildx bake config

src:
	./src.sh

run: out/vmlinuz
	./run.sh

debug: out/vmlinuz
	./debug.sh

# Compiles only. Linking needs lld, which is not on the host — that happens in
# the container once the runtime stage exists.
# compile_commands.json is a prerequisite so the editor cannot fall behind the
# build: `clean` removes it, and `tidy` was previously the only target that
# asked for it back. Without it clangd guesses the flags, loses -std=c23, and
# reports nullptr/constexpr as undeclared in code that compiles.
check: compile_commands.json $(OBJ)

# Checks and WarningsAsErrors live in .clang-tidy; flags come from
# compile_commands.json, so there is no second copy to drift.
tidy: compile_commands.json
	clang-tidy --quiet $(wildcard src/*.c)

# -MMD -MP emits a .d per object listing the headers it read; -include feeds
# those back so editing syscall.h rebuilds everything that includes it.
DEPFLAGS := -MMD -MP

out/obj/%.o: src/%.c Makefile | $(UAPI)
	@mkdir -p $(@D)
	$(CLANG) --target=$(TRIPLE) $(DEPFLAGS) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

out/obj/%.o: src/%.S Makefile | $(UAPI)
	@mkdir -p $(@D)
	$(CLANG) --target=$(TRIPLE) $(DEPFLAGS) $(CPPFLAGS) -ffreestanding -g -c $< -o $@

-include $(OBJ:.o=.d)

# Generated rather than hand-written so the editor and the build cannot drift.
# The source list is a prerequisite, not just the Makefile: a new src/*.c is
# newer than the database that omits it, which is the only thing that makes
# adding a file regenerate rather than leaving clangd to infer its flags.
compile_commands.json: Makefile $(wildcard src/*.c)
	@printf '[' > $@
	@sep=""; for f in $(wildcard src/*.c); do \
	  printf '%s\n  {"directory": "%s", "file": "%s", "command": "%s"}' \
	    "$$sep" "$(CURDIR)" "$(CURDIR)/$$f" \
	    "$(CLANG) --target=$(TRIPLE) $(CPPFLAGS) $(CFLAGS) -c $$f" >> $@; \
	  sep=","; done
	@printf '\n]\n' >> $@

clean:
	rm -rf out/obj out/vmlinuz out/kernel.config out/System.map out/uapi compile_commands.json

distclean:
	rm -rf out
