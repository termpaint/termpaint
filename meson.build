project('termpaint', ['c', 'cpp'], default_options : ['buildtype=debugoptimized'])

if meson.get_compiler('cpp').get_id() == 'gcc' or meson.get_compiler('cpp').get_id() == 'clang'
  add_project_arguments('-fvisibility=hidden', '-fvisibility-inlines-hidden', '-DTERMPAINT_EXPORT_SYMBOLS', language: 'c')
  add_project_arguments('-fvisibility=hidden', '-fvisibility-inlines-hidden', '-DTERMPAINT_EXPORT_SYMBOLS', language: 'cpp')
endif

add_project_arguments('-std=gnu11', language : 'c')
add_project_arguments('-Wall', '-Wextra', '-Werror=strict-prototypes', language: 'c')
add_project_arguments('-Werror=return-type', '-Werror=implicit-function-declaration', language: 'c')

add_project_arguments('-std=gnu++14', language : 'cpp')
add_project_arguments('-Wall', '-Wextra', language: 'cpp')
add_project_arguments('-Werror=return-type', '-Werror=implicit-function-declaration', language: 'cpp')

if get_option('valgrind-compat')
    add_project_arguments('-DTERMPAINTP_VALGRIND', language : 'c')
endif

main_lib_cargs = []

if get_option('errorlog')
  debugwin_inc = custom_target('debugwin_inc', output: ['debugwin.py.inc'],
      command: [find_program('./filetoinc.py'), 'debugwin', files('debugwin.py'), '@OUTPUT0@'])
  main_lib_cargs += '-DUSE_TK_DEBUGLOG'
else
  debugwin_inc = []
endif

#ide:editable-filelist
main_lib_files = [
  'termpaint.c',
  'termpaint_event.c',
  'termpaint_input.c',
  'termpaintx.c',
  'termpaintx_ttyrescue.c',
  'ttyrescue.c',
  debugwin_inc
]

main_lib_cargs += '-DTERMPAINT_RESCUE_EMBEDDED'
main_lib = library('termpaint', main_lib_files,
  c_args: main_lib_cargs,
  install: true)

main_lib_installed_headers = [
  'termpaint.h',
  'termpaint_event.h',
  'termpaint_input.h',
  'termpaintx.h',
  'termpaintx_ttyrescue.h',
]

install_headers(main_lib_installed_headers)

import('pkgconfig').generate(main_lib,
  subdirs : '.',
  version : '0.0',
  name : 'libtermpaint',
  filebase : 'termpaint',
  description : 'A terminal cell display and keyboard abstraction library.',
)

#ide:editable-filelist
ttyrescue_files = [
  'ttyrescue.c',
]

executable('ttyrescue', ttyrescue_files)

#ide:editable-filelist
image_lib_files = [
  'termpaint_image.cpp',
]

image_lib = library('termpaint_image', image_lib_files, link_with: [main_lib],
                    install: true)

import('pkgconfig').generate(image_lib,
  subdirs : '.',
  version : '0.0',
  name : 'libtermpaint_image',
  filebase : 'termpaint_image',
  description : 'File load and save support for termpaint.',
)


#ide:editable-filelist
playground_files = [
  'playground2.cpp',
]

executable('playground', playground_files, link_with: [main_lib])

#ide:editable-filelist
attrdemo_files = [
  'demo/attrs.c',
]

executable('attrdemo', attrdemo_files, link_with: [main_lib])


#ide:editable-filelist
chardemo_files = [
  'demo/chars.c',
]

executable('chardemo', chardemo_files, link_with: [main_lib])

#ide:editable-filelist
detect_files = [
  'demo/detect.c',
]

executable('detect', detect_files, link_with: [main_lib])

#ide:editable-filelist
keyboardcollector_files = [
  'tools/keyboardcollector.cpp',
]


executable('keyboardcollector', keyboardcollector_files, link_with: [main_lib])
if get_option('ssh')
  executable('keyboardcollector-ssh', 'tools/keyboardcollector.cpp', 'tools/SshServer.cpp',
    cpp_args: ['-DUSE_SSH'],
    link_with: [main_lib],
    dependencies: dependency('libssh'))
endif
docopt_lib = static_library('libdocopt', 'third-party/docopt/docopt.cpp', cpp_args: ['-Wno-unknown-pragmas'])
fmt_lib = static_library('libfmt', 'third-party/format.cc')
executable('mcheck', 'tools/mcheck.cpp', link_with: [main_lib, docopt_lib, fmt_lib])
executable('termquery', 'termquery.cpp', link_with: [main_lib])

testlib = static_library('testlib', 'tests/catch_main.cpp')

#ide:editable-filelist
test_files = [
  'tests/input_tests.cpp',
  'tests/measurement_tests.cpp',
  'tests/utf8_tests.cpp',
]

test('testtermpaint', executable('testtermpaint', test_files, link_with: [main_lib, testlib], cpp_args: ['-fno-inline']))
