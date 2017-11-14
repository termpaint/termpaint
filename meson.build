project('termpaint', ['c', 'cpp'], default_options : ['buildtype=debugoptimized'])

if meson.get_compiler('cpp').get_id() == 'gcc'
  add_project_arguments('-fvisibility=hidden', '-fvisibility-inlines-hidden', language: 'cpp')
endif

add_project_arguments('-std=gnu11', language : 'c')
add_project_arguments('-Wall', '-Wextra', language: 'c')
add_project_arguments('-Werror=return-type', '-Werror=implicit-function-declaration', language: 'c')

add_project_arguments('-std=gnu++14', language : 'cpp')
add_project_arguments('-Wall', '-Wextra', language: 'cpp')
add_project_arguments('-Werror=return-type', '-Werror=implicit-function-declaration', language: 'cpp')

main_lib = library('termpaint', 'termpaint.c', 'termpaintx.c', 'termpaint_input.c', install: true)

main_lib_installed_headers = [
  'termpaint.h',
  'termpaint_input.h',
  'termpaintx.h',
]

install_headers(main_lib_installed_headers)

import('pkgconfig').generate(
  libraries : [main_lib],
  subdirs : '.',
  version : '0.0',
  name : 'libtermpaint',
  filebase : 'termpaint',
  description : 'A terminal cell display and keyboard abstraction library.',
)

executable('playground', 'playground2.cpp', link_with: [main_lib])
executable('keyboardcollector', 'tools/keyboardcollector.cpp', link_with: [main_lib])
if get_option('ssh')
  executable('keyboardcollector-ssh', 'tools/keyboardcollector.cpp', 'tools/SshServer.cpp',
    cpp_args: ['-DUSE_SSH'],
    link_with: [main_lib],
    dependencies: dependency('libssh'))
endif
executable('termquery', 'termquery.cpp', link_with: [main_lib])

testlib = static_library('testlib', 'tests/catch_main.cpp')
test('testtermpaint', executable('testtermpaint', 'tests/input_tests.cpp', 'tests/utf8_tests.cpp', link_with: [main_lib, testlib]))
