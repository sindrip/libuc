project(
  'libuc',
  'c',
  version: '0.0.0',
  meson_version: '>=1.10.0',
  default_options: [
    'b_asneeded=false',
    'b_lundef=true',
    'b_pie=false',
    'b_staticpic=false',
    'buildtype=custom',
    'c_std=c23',
    'debug=true',
    'default_library=static',
    'libdir=lib',
    'optimization=1',
    'prefix=/usr',
    'warning_level=0',
    'werror=false',
  ],
)

fs = import('fs')
cc = meson.get_compiler('c')

if cc.get_id() != 'clang'
  error('libuc is built with Clang, not ' + cc.get_id())
endif

linker = cc.get_linker_id()
if linker != 'ld.lld' and linker != 'lld'
  error('libuc is linked with LLD, not ' + linker)
endif

arch = host_machine.cpu_family()
if arch != 'aarch64' and arch != 'x86_64'
  error('unsupported libuc architecture: ' + arch)
endif

uapi_path = meson.get_external_property('uapi_include')
if not fs.is_dir(uapi_path)
  error('kernel UAPI export is missing: ' + uapi_path)
endif

public_include = include_directories('include')

base_c_args = [
  '-ffreestanding',
  '-fno-omit-frame-pointer',
  '-fno-stack-protector',
  '-isystem',
  uapi_path,
  '-nostdlibinc',
  # Meson assumes it is compiling against an existing Unix libc and defines
  # this unconditionally. libuc owns that ABI choice, so cancel the default.
  '-U_FILE_OFFSET_BITS',
]

strict_warnings = [
  '-Weverything',
  '-Wno-c++-compat',
  '-Wno-c++98-compat',
  '-Wno-c++98-compat-pedantic',
  '-Wno-declaration-after-statement',
  '-Wno-pre-c11-compat',
  '-Wno-pre-c23-compat',
  '-Wno-unsafe-buffer-usage',
  '-Wno-unused-command-line-argument',
  '-Werror',
]

ubsan_want = [
  '-fsanitize=undefined,local-bounds',
  '-fsanitize-minimal-runtime',
  '-fno-sanitize-recover=all',
]
ubsan_args = []
ubsan_option = get_option('ubsan')

if not ubsan_option.disabled()
  if cc.has_multi_arguments(ubsan_want)
    ubsan_args = ubsan_want
  elif ubsan_option.enabled()
    error('the selected Clang does not support freestanding minimal UBSan')
  endif
endif

common_c_args = base_c_args + strict_warnings + ubsan_args

# Meson intentionally applies compiler arguments per target, not per source.
# Keep the few implementation translation units with special contracts in
# private archives, then fold every object into the one installed libc.a.
startup_impl = static_library(
  'uc-startup-internal',
  'src/start.c',
  build_by_default: false,
  c_args: common_c_args + ['-Wno-reserved-identifier'],
  include_directories: public_include,
  pic: false,
)

ubsan_impl = static_library(
  'uc-ubsan-internal',
  'src/ubsan.c',
  build_by_default: false,
  c_args: common_c_args + [
    '-Wno-empty-translation-unit',
    '-Wno-reserved-identifier',
  ],
  include_directories: public_include,
  pic: false,
)

memcmp_impl = static_library(
  'uc-memcmp-internal',
  'src/string/memcmp.c',
  build_by_default: false,
  c_args: common_c_args + ['-fno-builtin'],
  include_directories: public_include,
  pic: false,
)

memcpy_impl = static_library(
  'uc-memcpy-internal',
  'src/string/memcpy.c',
  build_by_default: false,
  c_args: common_c_args + ['-fno-builtin'],
  include_directories: public_include,
  pic: false,
)

memmove_impl = static_library(
  'uc-memmove-internal',
  'src/string/memmove.c',
  build_by_default: false,
  c_args: common_c_args + ['-fno-builtin'],
  include_directories: public_include,
  pic: false,
)

memset_impl = static_library(
  'uc-memset-internal',
  'src/string/memset.c',
  build_by_default: false,
  c_args: common_c_args + ['-fno-builtin'],
  include_directories: public_include,
  pic: false,
)

libc = static_library(
  'c',
  build_by_default: true,
  install: true,
  link_whole: [
    startup_impl,
    ubsan_impl,
    memcmp_impl,
    memcpy_impl,
    memmove_impl,
    memset_impl,
  ],
  pic: false,
)

install_headers('include/string.h')

summary(
  {
    'architecture': arch,
    'kernel UAPI': uapi_path,
    'minimal UBSan': ubsan_args.length() != 0,
  },
  section: 'libuc target',
)

if get_option('tests')
  # This remains a private archive because the shipped CRT will eventually be
  # an installed crt1.o. link_whole gives today's probes the same direct-object
  # contract as the Make build without teaching generic code an architecture.
  crt1_internal = static_library(
    'uc-crt1-internal',
    'src/arch/' + arch + '/start.S',
    build_by_default: false,
    c_args: base_c_args,
    override_options: ['c_std=none'],
    pic: false,
  )

  linker_script = meson.current_source_dir() / 'libuc.ld'
  probe_link_args = [
    '-nostartfiles',
    '-nostdlib',
    '-static',
    '-Wl,-T,' + linker_script,
    '-Wl,-z,norelro',
  ]

  start_probe = executable(
    'start',
    'test/main.c',
    c_args: common_c_args,
    include_directories: public_include,
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  # Do not run target code unless the machine file says it is executable here
  # or supplies an exe_wrapper.
  if meson.can_run_host_binaries()
    test('startup contract', start_probe)
  else
    message('probe execution disabled: add cross/runnable.ini in a matching Linux environment')
  endif

  sh = find_program('sh', native: true)
  mkinitramfs = files('tools/mkinitramfs.sh')

  start_initramfs = custom_target(
    'start-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: start_probe,
    output: 'start.initramfs.cpio.gz',
  )

  alias_target('initramfs', start_initramfs)
endif
