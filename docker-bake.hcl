variable "KERNEL_VERSION" { default = "7.2" }
variable "KERNEL_SHA256" { default = "f9fef3d14c0df53819026f4be74459835c2a0b0dcbf5b5bbd9ea19f0829402b3" }

target "kernel" {
  dockerfile = "build/kernel.Dockerfile"
  target     = "kernel"
  args = {
    KERNEL_VERSION = KERNEL_VERSION
    KERNEL_SHA256  = KERNEL_SHA256
  }
  output = ["type=local,dest=out"]
}

target "config" {
  dockerfile = "build/kernel.Dockerfile"
  target     = "config"
  args = {
    KERNEL_VERSION = KERNEL_VERSION
    KERNEL_SHA256  = KERNEL_SHA256
  }
  output = ["type=local,dest=out"]
}

target "src" {
  dockerfile = "build/kernel.Dockerfile"
  target     = "linux-src"
  args = {
    KERNEL_VERSION = KERNEL_VERSION
    KERNEL_SHA256  = KERNEL_SHA256
  }
  output = ["type=tar,dest=out/linux-src.tar"]
}

target "uapi" {
  dockerfile = "build/kernel.Dockerfile"
  target     = "uapi"
  args = {
    KERNEL_VERSION = KERNEL_VERSION
    KERNEL_SHA256  = KERNEL_SHA256
  }
  output = ["type=local,dest=out/uapi"]
}

target "uapi-x86_64" {
  dockerfile = "build/kernel.Dockerfile"
  target     = "uapi-x86_64"
  args = {
    KERNEL_VERSION = KERNEL_VERSION
    KERNEL_SHA256  = KERNEL_SHA256
  }
  output = ["type=local,dest=out/uapi-x86_64"]
}

group "default" {
  targets = ["kernel"]
}
