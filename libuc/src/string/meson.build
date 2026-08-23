string_impls = [
  static_library(
    'uc-memcmp-internal',
    'memcmp.c',
    build_by_default: false,
    c_args: common_c_args + ['-fno-builtin'],
    include_directories: public_include,
    pic: false,
  ),
  static_library(
    'uc-memcpy-internal',
    'memcpy.c',
    build_by_default: false,
    c_args: common_c_args + ['-fno-builtin'],
    include_directories: public_include,
    pic: false,
  ),
  static_library(
    'uc-memmove-internal',
    'memmove.c',
    build_by_default: false,
    c_args: common_c_args + ['-fno-builtin'],
    include_directories: public_include,
    pic: false,
  ),
  static_library(
    'uc-memset-internal',
    'memset.c',
    build_by_default: false,
    c_args: common_c_args + ['-fno-builtin'],
    include_directories: public_include,
    pic: false,
  ),
]
