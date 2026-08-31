project(
  'libuc',
  'c',
  version: '0.0.0',
  license: 'MIT',
  license_files: 'LICENSE',
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

subdir('src/thread_local')
subdir('src/fiber')
subdir('src/ring')
subdir('src/scheduler')
subdir('src/errno')
subdir('src/sys/auxv')
subdir('src/sys/socket')
subdir('src/unistd')

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

# errno is the only thread-local datum the libc itself contributes. Probes
# that assert an exact PT_TLS layout — their own TU's, or its absence —
# link these archives; linking the installed libc means inheriting errno.
# TUs that set errno ride with it.
no_thread_local_impls = libc_impls + string_impls

libc_impls += [errno_impls, getauxval_impl] + unistd_impls + socket_impls

libc = static_library(
  'c',
  build_by_default: true,
  install: true,
  link_whole: libc_impls + string_impls,
  pic: false,
)

install_data('LICENSE', install_dir: '.')
install_headers('include/errno.h')
install_headers('include/fcntl.h')
install_headers('include/string.h')
install_headers('include/unistd.h')
install_headers('include/netinet/in.h', subdir: 'netinet')
install_headers('include/sys/auxv.h', subdir: 'sys')
install_headers('include/sys/socket.h', subdir: 'sys')
install_headers('include/sys/types.h', subdir: 'sys')

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
    link_whole: [crt1_internal] + no_thread_local_impls,
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
    link_whole: [crt1_internal] + no_thread_local_impls,
    name_suffix: 'elf',
  )

  thread_local_block_probe = executable(
    'thread-local-block',
    'test/thread_local_block.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal] + no_thread_local_impls,
    name_suffix: 'elf',
  )

  socket_echo_probe = executable(
    'socket-echo',
    'test/socket_echo.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include, fiber_arch_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  libc_io_probe = executable(
    'libc-io',
    'test/libc_io.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include, fiber_arch_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  pipe_probe = executable(
    'pipe',
    'test/pipe.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include, fiber_arch_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  close_probe = executable(
    'close',
    'test/close.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include, fiber_arch_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  getauxval_probe = executable(
    'getauxval',
    'test/getauxval.c',
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

  scheduler_probe = executable(
    'scheduler',
    'test/scheduler.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include, fiber_arch_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  fiber_io_probe = executable(
    'fiber-io',
    'test/fiber_io.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include, fiber_arch_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  fiber_io_flood_probe = executable(
    'fiber-io-flood',
    'test/fiber_io_flood.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include, fiber_arch_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  sweep_generation_probe = executable(
    'sweep-generation',
    'test/sweep_generation.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include, fiber_arch_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

  test_arch_include = include_directories('test/arch/' + arch)

  # A benchmark, not a test: run by hand on a -Dubsan=disabled setup.
  bench_await_probe = executable(
    'bench-await',
    'test/bench_await.c',
    c_args: common_c_args + ['-Wno-reserved-identifier'],
    include_directories: [public_include, private_include, fiber_arch_include,
                          test_arch_include],
    link_args: probe_link_args,
    link_depends: 'libuc.ld',
    link_whole: [crt1_internal, libc],
    name_suffix: 'elf',
  )

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
    test('getauxval hit and ENOENT miss', getauxval_probe)
    test('close suspends on the ring', close_probe)
    test('pipe descriptors arrive through the ring', pipe_probe)
    test('read suspends until write completes it', libc_io_probe)
    test('sockets arrive through the ring', socket_echo_probe)
    test('thread-local install alternation', thread_local_install_probe)
    test('fiber bootstrap round trip', fiber_probe)
    test('fiber thread-local binding', fiber_thread_local_probe)
    test('ring nop round trip', ring_probe)
    test('scheduler round robin', scheduler_probe)
    test('fiber parks on the ring', fiber_io_probe)
    test('ring batch overflows the sweep', fiber_io_flood_probe)
    test('yielders do not starve completions', sweep_generation_probe)
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

  socket_echo_initramfs = custom_target(
    'socket-echo-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: socket_echo_probe,
    output: 'socket-echo.initramfs.cpio.gz',
  )

  alias_target('initramfs-socket-echo', socket_echo_initramfs)

  libc_io_initramfs = custom_target(
    'libc-io-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: libc_io_probe,
    output: 'libc-io.initramfs.cpio.gz',
  )

  alias_target('initramfs-libc-io', libc_io_initramfs)

  pipe_initramfs = custom_target(
    'pipe-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: pipe_probe,
    output: 'pipe.initramfs.cpio.gz',
  )

  alias_target('initramfs-pipe', pipe_initramfs)

  close_initramfs = custom_target(
    'close-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: close_probe,
    output: 'close.initramfs.cpio.gz',
  )

  alias_target('initramfs-close', close_initramfs)

  getauxval_initramfs = custom_target(
    'getauxval-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: getauxval_probe,
    output: 'getauxval.initramfs.cpio.gz',
  )

  alias_target('initramfs-getauxval', getauxval_initramfs)

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

  scheduler_initramfs = custom_target(
    'scheduler-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: scheduler_probe,
    output: 'scheduler.initramfs.cpio.gz',
  )

  alias_target('initramfs-scheduler', scheduler_initramfs)

  fiber_io_initramfs = custom_target(
    'fiber-io-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: fiber_io_probe,
    output: 'fiber-io.initramfs.cpio.gz',
  )

  alias_target('initramfs-fiber-io', fiber_io_initramfs)

  fiber_io_flood_initramfs = custom_target(
    'fiber-io-flood-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: fiber_io_flood_probe,
    output: 'fiber-io-flood.initramfs.cpio.gz',
  )

  alias_target('initramfs-fiber-io-flood', fiber_io_flood_initramfs)

  bench_await_initramfs = custom_target(
    'bench-await-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: bench_await_probe,
    output: 'bench-await.initramfs.cpio.gz',
  )

  alias_target('initramfs-bench-await', bench_await_initramfs)

  sweep_generation_initramfs = custom_target(
    'sweep-generation-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: sweep_generation_probe,
    output: 'sweep-generation.initramfs.cpio.gz',
  )

  alias_target('initramfs-sweep-generation', sweep_generation_initramfs)

  exit_status_initramfs = custom_target(
    'exit-status-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: exit_status_probe,
    output: 'exit-status.initramfs.cpio.gz',
  )

  alias_target('initramfs-exit-status', exit_status_initramfs)

  fiber_initramfs = custom_target(
    'fiber-initramfs',
    build_by_default: false,
    command: [sh, mkinitramfs, '@INPUT@', '@OUTPUT@'],
    input: fiber_probe,
    output: 'fiber.initramfs.cpio.gz',
  )

  alias_target('initramfs-fiber', fiber_initramfs)

  alias_target(
    'initramfs-all',
    start_initramfs,
    start_no_thread_local_initramfs,
    libc_io_initramfs,
    pipe_initramfs,
    close_initramfs,
    getauxval_initramfs,
    thread_local_block_initramfs,
    thread_local_install_initramfs,
    fiber_thread_local_initramfs,
    ring_initramfs,
    scheduler_initramfs,
    fiber_io_initramfs,
    fiber_io_flood_initramfs,
    sweep_generation_initramfs,
    exit_status_initramfs,
    fiber_initramfs,
  )
endif
