variable "KERNEL_VERSION" { default = "7.2" }
variable "KERNEL_SHA256" { default = "f9fef3d14c0df53819026f4be74459835c2a0b0dcbf5b5bbd9ea19f0829402b3" }

# Bake merges inherited `args` per key, so a child target states nothing
# but what it changes.
target "_common" {
  dockerfile = "build/kernel.Dockerfile"
  args = {
    KERNEL_VERSION = KERNEL_VERSION
    KERNEL_SHA256  = KERNEL_SHA256
  }
}

target "kernel" {
  inherits  = ["_common"]
  target    = "kernel"
  platforms = ["linux/arm64"]
  args = {
    ARCH         = "arm64"
    KERNEL_IMAGE = "arch/arm64/boot/Image"
  }
  output = ["type=local,dest=out"]
}

# The same recipe pointed at the other architecture. The toolchain must run
# as amd64 (Rosetta on this Mac, native in CI): alpine's arm64 gcc cannot
# produce an x86_64 kernel.
target "kernel-x86_64" {
  inherits  = ["kernel"]
  platforms = ["linux/amd64"]
  args = {
    ARCH         = "x86_64"
    KERNEL_IMAGE = "arch/x86/boot/bzImage"
  }
  output = ["type=local,dest=out/kernel-x86_64"]
}

target "config" {
  inherits = ["_common"]
  target   = "config"
  output   = ["type=local,dest=out"]
}

target "src" {
  inherits = ["_common"]
  target   = "linux-src"
  output   = ["type=tar,dest=out/linux-src.tar"]
}

# Multi-platform exports split into linux_<TARGETARCH> subdirectories; the
# cross files point there.
target "uapi" {
  inherits  = ["_common"]
  target    = "uapi"
  platforms = ["linux/arm64", "linux/amd64"]
  output    = ["type=local,dest=out/uapi"]
}

target "libuc" {
  dockerfile = "build/libuc.Dockerfile"
  target     = "libuc"
  platforms  = ["linux/arm64", "linux/amd64"]
  contexts   = { uapi = "target:uapi" }
  output     = ["type=local,dest=out/libuc"]
}

group "default" {
  targets = ["kernel"]
}
