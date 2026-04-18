project('cache_profiler', 'c',
  version: '0.1.0',
  default_options: ['warning_level=3', 'c_std=c11'])

inc = include_directories('include')

cache_profiler_lib = library('cache_profiler',
  [
    'src/cache_profiler.c',
    'src/profiler.c',
    'src/logger.c',
  ],
  include_directories: inc,
  install: true,
  version: meson.project_version())

install_headers(
  [
    'include/cache_profiler.h',
    'include/profiler.h',
    'include/logger.h',
  ])

cache_profiler_bin = executable('cache_profiler',
  'src/cache_profiler_main.c',
  include_directories: inc,
  link_with: cache_profiler_lib)
