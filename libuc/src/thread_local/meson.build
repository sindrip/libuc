thread_local_impls = static_library(
  'uc-thread-local-internal',
  'thread_local.c',
  build_by_default: false,
  c_args: common_c_args + ['-Wno-reserved-identifier'],
  include_directories: [public_include, private_include],
  pic: false,
)
