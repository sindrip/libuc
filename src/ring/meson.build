ring_arch_include = include_directories('../arch/' + arch)

ring_impls = static_library(
  'uc-ring-internal',
  'ring.c',
  build_by_default: false,
  c_args: common_c_args + ['-Wno-reserved-identifier'],
  include_directories: [public_include, private_include, ring_arch_include],
  pic: false,
)
