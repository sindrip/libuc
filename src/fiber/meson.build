fiber_arch_include = include_directories('../arch/' + arch)

fiber_impls = static_library(
  'uc-fiber-internal',
  'fiber.c',
  build_by_default: false,
  c_args: common_c_args + ['-Wno-reserved-identifier'],
  include_directories: [public_include, private_include, fiber_arch_include],
  pic: false,
)
