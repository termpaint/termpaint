project('termpaint', ['c', 'cpp'], default_options : ['buildtype=debugoptimized'])

if meson.get_compiler('cpp').get_id() == 'gcc' or meson.get_compiler('cpp').get_id() == 'clang'
  add_project_arguments('-fvisibility=hidden', '-DTERMPAINT_EXPORT_SYMBOLS', language: 'c')
  add_project_arguments('-fvisibility=hidden', '-fvisibility-inlines-hidden', '-DTERMPAINT_EXPORT_SYMBOLS', language: 'cpp')
endif

add_project_arguments('-std=gnu11', language : 'c')
add_project_arguments('-Wall', '-Wextra', '-Werror=strict-prototypes', '-Werror=incompatible-pointer-types',
                      '-Werror=implicit-int', '-Werror=discarded-qualifiers',
                      language: 'c')
add_project_arguments('-Werror=return-type', '-Werror=implicit-function-declaration', language: 'c')

add_project_arguments('-std=gnu++14', language : 'cpp')
add_project_arguments('-Wall', '-Wextra', language: 'cpp')
add_project_arguments('-Werror=return-type', '-Werror=implicit-function-declaration',
                      '-Werror=incompatible-pointer-types', '-Werror=discarded-qualifiers',
                      '-Werror=implicit-int', language: 'cpp')

if get_option('valgrind-compat')
    add_project_arguments('-DTERMPAINTP_VALGRIND', language : 'c')
endif

cc = meson.get_compiler('c')
lib_rt = cc.find_library('rt', required : false) # clock_gettime

silence_warnings = [
    '-Wno-padded'
]

main_lib_cargs = []

###### nolibc ttyrescue
# This option should only default to true on linux (or other supported platforms). But meson
# does not really allow doing that in a reasonable way. So just ignore it on other platforms.
if get_option('ttyrescue-fexec-blob') and host_machine.system() == 'linux'
  ttyrescue_nolibc_exe = executable('ttyrescue_nolibc', 'ttyrescue_nolibc.c',
    cpp_args: ['-fno-asynchronous-unwind-tables', '-fno-ident', '-nostdlib', '-static', '-Os',
               '-fvisibility=hidden', '-std=gnu11'],
    link_args: ['-fno-asynchronous-unwind-tables', '-fno-ident', '-nostdlib', '-static', '-Os',
                '-fvisibility=hidden', '-std=gnu11'])

  ttyrescue_nolibc_stripped = custom_target('ttyrescue_nolibc_stripped',
    input: ttyrescue_nolibc_exe,
    output: 'ttyrescue_nolibc_stripped',
    command: [find_program('strip'), '--strip-all', '--remove-section=.comment',
              '--remove-section=.note', '--remove-section=.eh_frame_hdr',
              '--remove-section=.eh_frame', '--remove-section=.note.gnu.gold-version',
              '--remove-section=.note.gnu.build-id',
              '@INPUT@', '-o', '@OUTPUT@'])

  ttyrescue_nolibc_inc = custom_target('ttyrescue_nolibc_inc',
    input: ttyrescue_nolibc_stripped,
    output: ['ttyrescue_nolibc.inc'],
    command: [find_program('./filetoinc.py'), 'ttyrescue_blob', '@INPUT@', '@OUTPUT0@'])

  main_lib_cargs += '-DTERMPAINT_RESCUE_FEXEC'
else
  ttyrescue_nolibc_inc = []
endif
###### /nolibc ttyrescue

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
  debugwin_inc,
  ttyrescue_nolibc_inc
]

main_lib_cargs += '-DTERMPAINT_RESCUE_EMBEDDED'
main_lib_cargs += '-DTERMPAINT_RESCUE_PATH="@0@"'.format(get_option('ttyrescue-path'))
main_lib = library('termpaint', main_lib_files,
  dependencies: lib_rt,
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

image_lib_installed_headers = [
  'termpaint_image.h',
]

install_headers(image_lib_installed_headers)

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
  'tests/hashtest.cpp',
  'tests/input_tests.cpp',
  'tests/measurement_tests.cpp',
  'tests/surface.cpp',
  'tests/utf8_tests.cpp',
]

test('testtermpaint', executable('testtermpaint', test_files, link_with: [main_lib, testlib], cpp_args: ['-fno-inline', silence_warnings]))

#ide:editable-filelist
test_terminaloutput_files = [
  'tests/terminaloutput.cpp',
  'tests/terminaloutput_main.cpp',
]

test('testtermpaint_terminaloutput', executable('testtermpaint_terminaloutput',
  test_terminaloutput_files,
  link_with: [main_lib],
  dependencies: dependency('threads'),
  cpp_args: ['-fno-inline', silence_warnings]))
