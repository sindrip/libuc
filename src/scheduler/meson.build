scheduler_arch_include = include_directories('../arch/' + arch)

scheduler_impls = static_library(
  'uc-scheduler-internal',
  'scheduler.c',
  build_by_default: false,
  c_args: common_c_args + ['-Wno-reserved-identifier'],
  include_directories: [public_include, private_include,
                        scheduler_arch_include],
  pic: false,
)
