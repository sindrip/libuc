auxv_impl = static_library(
  'uc-auxv-internal',
  'auxv.c',
  build_by_default: false,
  c_args: common_c_args + ['-Wno-reserved-identifier'],
  include_directories: [public_include, private_include],
  pic: false,
)

getauxval_impl = static_library(
  'uc-getauxval-internal',
  'getauxval.c',
  build_by_default: false,
  c_args: common_c_args + ['-Wno-reserved-identifier'],
  include_directories: [public_include, private_include],
  pic: false,
)
