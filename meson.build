project('cache_sampler', ['c', 'cpp'],
  version: '0.1.0',
  default_options: ['warning_level=3', 'c_std=c11', 'cpp_std=c++17'])

inc = include_directories('include')

libbpf_dep = dependency('libbpf', required: true)
clang_bin = find_program('clang', required: true)

bpf_c_args = [
  '-O2',
  '-g',
  '-target', 'bpf',
  '-D__TARGET_ARCH_x86',
]

multiarch = run_command('cc', '-print-multiarch', check: false).stdout().strip()
if multiarch != ''
  bpf_c_args += ['-I' + join_paths('/usr/include', multiarch)]
endif

custom_target('cache-sampler-bpf-object',
  input: 'src/bpf/cache_sampler.bpf.c',
  output: 'cache_sampler.bpf.o',
  command: [clang_bin] + bpf_c_args + ['-c', '@INPUT@', '-o', '@OUTPUT@'],
  build_by_default: true,
  install: true,
  install_dir: join_paths(get_option('datadir'), 'cache_sampler'))

cache_profiler_lib = library('cacheprofiler',
  [
    'src/CacheProfilerApp.cpp',
    'src/EBpfCacheProfiler.cpp',
    'src/Logger.cpp',
    'src/TerminalCacheSampleLogger.cpp',
  ],
  include_directories: inc,
  dependencies: [libbpf_dep],
  install: true)

cache_profiler_bin = executable('cache_profiler',
  [
    'src/client/CliParsing.cpp',
    'src/client/Main.cpp',
  ],
  include_directories: inc,
  link_with: cache_profiler_lib,
  dependencies: [libbpf_dep])

install_headers(
  'include/CacheSample.h',
  'include/CacheProfilerApp.h',
  'include/ICacheProfiler.h',
  'include/EBpfCacheProfiler.h',
  'include/ProfilingConfig.h',
  'include/ICacheSampleLogger.h',
  'include/TerminalCacheSampleLogger.h')
