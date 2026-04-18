project('printmsg', 'c',
  version: '0.1.0',
  default_options: ['warning_level=3', 'c_std=c11'])

inc = include_directories('include')

printmsg_lib = library('printmsg',
  ['src/printmsg.c'],
  include_directories: inc,
  install: true,
  version: meson.project_version())

install_headers('include/printmsg.h')

ebpf_example = executable('ebpf_main',
  'examples/ebpf_main.c',
  include_directories: inc,
  link_with: printmsg_lib)
