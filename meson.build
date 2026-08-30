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
    'optimization=2',
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
private_include = include_directories('src')
arch_include = include_directories('src/arch/' + arch)

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
  # -Weverything carries both -Wswitch-default and -Wcovered-switch-default:
  # one demands a default label on every switch, the other rejects a default
  # that covers all enumeration values. A fully-covered enum switch cannot
  # satisfy both, so -Weverything forces a choice rather than expressing one.
  # Suppressing this one keeps covered-switch-default, which is what turns a
  # new enumerator into a build failure instead of a silent fall-through.
  '-Wno-switch-default',
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
  include_directories: [public_include, private_include, arch_include],
  pic: false,
)

auxv_impl = static_library(
  'uc-auxv-internal',
  'src/auxv.c',
  build_by_default: false,
  c_args: common_c_args + [
    '-Wno-missing-noreturn',
    '-Wno-reserved-identifier',
  ],
  include_directories: [public_include, private_include],
  pic: false,
)

subdir('src/thread_local')
subdir('src/fiber')
subdir('src/ring')
subdir('src/scheduler')

libc_impls = [startup_impl, auxv_impl, thread_local_impls, fiber_impls,
              ring_impls, scheduler_impls]

if ubsan_args.length() != 0
  libc_impls += static_library(
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
endif

subdir('src/string')

libc = static_library(
  'c',
  build_by_default: true,
  install: true,
  link_whole: libc_impls + string_impls,
  pic: false,
)

install_headers('include/string.h')
install_headers('include/sys/auxv.h', subdir: 'sys')

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
    'src/arch/' + arch + '/start.c',
    build_by_default: false,
    c_args: common_c_args + ['-Wno-reserved-identifier'],
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
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  exit_status_probe = executable(
    'exit-status',
    'test/exit_status.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  start_no_thread_local_probe = executable(
    'start-no-thread-local',
    'test/no_thread_local.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include, fiber_arch_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  thread_local_block_probe = executable(
    'thread-local-block',
    'test/thread_local_block.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  thread_local_install_probe = executable(
    'thread-local-install',
    'test/thread_local_install.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  fiber_thread_local_probe = executable(
    'fiber-thread-local',
    'test/fiber_thread_local.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include, fiber_arch_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  ring_probe = executable(
    'ring',
    'test/ring.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include, fiber_arch_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  test_arch_include = include_directories('test/arch/' + arch)

  fiber_probe = executable(
    'fiber',
    'test/fiber.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include, fiber_arch_include,
                          test_arch_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  sh = find_program('sh', native: true)
  llvm_readelf = find_program('llvm-readelf', native: true)
  llvm_nm = find_program('llvm-nm', native: true)

  test(
    'no scheduler symbol',
    sh,
    args: ['-c', 'symbols=$("$1" --defined-only "$2") || exit 1; ' +
           '! printf %s "$symbols" | grep -qi sched', 'check',
           llvm_nm, libc],
  )
  program_headers_test = files('test/program_headers.sh')

  test(
    'startup program headers',
    sh,
    args: [
      program_headers_test,
      llvm_readelf.full_path(),
      start_no_thread_local_probe,
      start_probe,
      thread_local_block_probe,
      thread_local_install_probe,
      fiber_thread_local_probe,
    ],
  )

  # Do not run target code unless the machine file says it is executable here
  # or supplies an exe_wrapper.
  if meson.can_run_host_binaries()
    test('startup contract with PT_TLS', start_probe)
    test('startup contract without PT_TLS', start_no_thread_local_probe)
    test('thread-local block with PT_TLS', thread_local_block_probe)
    test('thread-local install alternation', thread_local_install_probe)
    test('fiber bootstrap round trip', fiber_probe)
    test('fiber thread-local binding', fiber_thread_local_probe)
    test('ring nop round trip', ring_probe)
    test(
      'main status reaches exit_group',
      sh,
      args: ['-c', '"$1"; test $? -eq 42', 'check', exit_status_probe],
    )
  else
    message('probe execution disabled: add cross/runnable.ini in a matching Linux environment')
  endif

  mkinitramfs = files('tools/mkinitramfs.sh')

  start_initramfs = custom_target(
    'start-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: start_probe,
    output: 'start.initramfs.cpio.gz',
  )

  alias_target('initramfs', start_initramfs)

  start_no_thread_local_initramfs = custom_target(
    'start-no-thread-local-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: start_no_thread_local_probe,
    output: 'start-no-thread-local.initramfs.cpio.gz',
  )

  alias_target(
    'initramfs-no-thread-local',
    start_no_thread_local_initramfs,
  )

  thread_local_block_initramfs = custom_target(
    'thread-local-block-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: thread_local_block_probe,
    output: 'thread-local-block.initramfs.cpio.gz',
  )

  alias_target(
    'initramfs-thread-local-block',
    thread_local_block_initramfs,
  )

  thread_local_install_initramfs = custom_target(
    'thread-local-install-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: thread_local_install_probe,
    output: 'thread-local-install.initramfs.cpio.gz',
  )

  alias_target(
    'initramfs-thread-local-install',
    thread_local_install_initramfs,
  )

  fiber_thread_local_initramfs = custom_target(
    'fiber-thread-local-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: fiber_thread_local_probe,
    output: 'fiber-thread-local.initramfs.cpio.gz',
  )

  alias_target(
    'initramfs-fiber-thread-local',
    fiber_thread_local_initramfs,
  )

  ring_initramfs = custom_target(
    'ring-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: ring_probe,
    output: 'ring.initramfs.cpio.gz',
  )

  alias_target('initramfs-ring', ring_initramfs)
endif
