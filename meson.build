# SPDX-License-Identifier: BSL-1.0
project('termpaint', ['c', 'cpp'],
  default_options : ['buildtype=debugoptimized', 'cpp_std=c++14', 'c_std=gnu11'],
  version: '0.3.1')

if meson.get_compiler('cpp').get_id() == 'gcc' or meson.get_compiler('cpp').get_id() == 'clang'
  add_project_arguments('-fvisibility=hidden', '-DTERMPAINT_EXPORT_SYMBOLS', language: 'c')
  add_project_arguments('-fvisibility=hidden', '-fvisibility-inlines-hidden', '-DTERMPAINT_EXPORT_SYMBOLS', language: 'cpp')
endif

cc = meson.get_compiler('c')

add_project_arguments(cc.get_supported_arguments('-Wall', '-Wextra',
                      '-Werror=strict-prototypes', '-Werror=incompatible-pointer-types',
                      '-Werror=implicit-int', '-Werror=discarded-qualifiers',
                      '-Wno-string-plus-int'), language: 'c')
add_project_arguments('-Werror=return-type', '-Werror=implicit-function-declaration', '-Werror=int-conversion', '-Werror=old-style-definition', language: 'c')

add_project_arguments('-Wall', '-Wextra', language: 'cpp')
add_project_arguments('-Werror=return-type', language: 'cpp')

if get_option('valgrind-compat')
    add_project_arguments('-DTERMPAINTP_VALGRIND', language : 'c')
endif

lib_rt = cc.find_library('rt', required : false) # clock_gettime

silence_warnings = [
    '-Wno-padded'
]

main_lib_cargs = []

###### blob ttyrescue
# This option should only default to true on linux (or other supported platforms). But meson
# does not really allow doing that in a reasonable way. So just ignore it on other platforms.
if get_option('ttyrescue-fexec-blob') and host_machine.system() == 'linux'
  ttyrescue_fexec_blob_type = get_option('ttyrescue-fexec-blob-type')
  if ttyrescue_fexec_blob_type == 'default'
    if (host_machine.cpu_family() == 'ppc64' or host_machine.cpu_family() == 's390x'
        or host_machine.cpu_family() == 'aarch64')
      # the vendored version of linux-syscall-support is known not to work on these architectures
      ttyrescue_fexec_blob_type = 'musl-gcc'
    else
      ttyrescue_fexec_blob_type = 'lss'
    endif
  endif
  if ttyrescue_fexec_blob_type == 'lss'
    ttyrescue_blob_exe = executable('ttyrescue_nolibc', 'ttyrescue_nolibc.c',
      c_args: ['-fno-asynchronous-unwind-tables', '-fno-ident', '-ffreestanding', '-nostdlib', '-static', '-Os',
                 '-fvisibility=hidden', '-std=gnu11'],
      link_args: ['-fno-asynchronous-unwind-tables', '-fno-ident', '-nostdlib', '-static', '-Os',
                  '-fvisibility=hidden', '-std=gnu11'])
  elif ttyrescue_fexec_blob_type == 'musl-gcc'
    # In the future this should be made cross compilation compatible
    ttyrescue_blob_exe = custom_target('ttyrescue_blob',
      input: 'ttyrescue_mini.c',
      output: 'ttyrescue_musl',
      command: [find_program('musl-gcc'), '-static', '-no-pie', '-Wl,-znoseparate-code,-z,norelro',
                '-fno-asynchronous-unwind-tables', '-fno-ident', '-Os', '-fvisibility=hidden', '-std=gnu11',
                '@INPUT@', '-o', '@OUTPUT@'])

  endif

  ttyrescue_blob_stripped = custom_target('ttyrescue_blob_stripped',
    input: ttyrescue_blob_exe,
    output: 'ttyrescue_blob_stripped',
    command: [find_program('strip'), '--strip-all', '--remove-section=.comment',
              '--remove-section=.note', '--remove-section=.eh_frame_hdr',
              '--remove-section=.eh_frame', '--remove-section=.note.gnu.gold-version',
              '--remove-section=.note.gnu.build-id',
              '@INPUT@', '-o', '@OUTPUT@'])

  ttyrescue_blob_inc = custom_target('ttyrescue_blob_inc',
    input: ttyrescue_blob_stripped,
    output: ['ttyrescue_blob.inc'],
    command: [find_program('./filetoinc.py'), 'ttyrescue_blob', '@INPUT@', '@OUTPUT0@'])

  main_lib_cargs += '-DTERMPAINT_RESCUE_FEXEC'
else
  ttyrescue_blob_inc = []
endif
###### /blob ttyrescue

if get_option('errorlog')
  debugwin_inc = custom_target('debugwin_inc', output: ['debugwin.py.inc'],
      command: [find_program('./filetoinc.py'), 'debugwin', files('debugwin.py'), '@OUTPUT0@'])
  main_lib_cargs += '-DUSE_TK_DEBUGLOG'
else
  debugwin_inc = []
endif

main_vscript = 'termpaint.symver'
if host_machine.system() == 'linux'
  # for now, only do this on linux, expand supported platforms as needed
  main_ld_vscript = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), main_vscript)
else
  main_ld_vscript = []
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
  ttyrescue_blob_inc
]

main_lib_cargs += '-DTERMPAINT_RESCUE_EMBEDDED'
main_lib_cargs += '-DTERMPAINT_RESCUE_PATH="@0@"'.format(get_option('ttyrescue-path'))
main_lib = library('termpaint', main_lib_files,
  dependencies: lib_rt,
  c_args: main_lib_cargs,
  soversion: '0a',
  darwin_versions: ['1', '1'],
  link_args : main_ld_vscript,
  link_depends : main_vscript,
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
  name : 'libtermpaint',
  filebase : 'termpaint',
  description : 'A terminal cell display and keyboard abstraction library.',
)

termpaint_dep = declare_dependency(
  include_directories: include_directories('.'),
  link_with: main_lib
)
if meson.version().version_compare('>=0.54')
  meson.override_dependency('termpaint', termpaint_dep)
endif

#ide:editable-filelist
ttyrescue_files = [
  'ttyrescue.c',
]

ttyrescue_exe_args = {}

if get_option('ttyrescue-install')
    ttyrescue_exe_args += {'install': true, 'install_dir': get_option('ttyrescue-path')}
endif

executable('ttyrescue', ttyrescue_files, kwargs: ttyrescue_exe_args)

if cc.has_header('picojson.h', required : get_option('system-picojson'))
  picojson_dep = []
else
  picojson_dep = declare_dependency(compile_args: ['-DBUNDLED_PICOJSON'])
endif

image_vscript = 'termpaint_image.symver'
if host_machine.system() == 'linux'
  image_ld_vscript = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), image_vscript)
else
  image_ld_vscript = []
endif

#ide:editable-filelist
image_lib_files = [
  'termpaint_image.cpp',
]

image_lib = library('termpaint_image', image_lib_files, link_with: [main_lib],
                    dependencies: [picojson_dep],
                    soversion: '0a',
                    darwin_versions: ['1', '1'],
                    link_args : image_ld_vscript,
                    link_depends : image_vscript,
                    install: true)

image_lib_installed_headers = [
  'termpaint_image.h',
]

install_headers(image_lib_installed_headers)

import('pkgconfig').generate(image_lib,
  subdirs : '.',
  name : 'libtermpaint_image',
  filebase : 'termpaint_image',
  description : 'File load and save support for termpaint.',
)

termpaint_image_dep = declare_dependency(
  include_directories: include_directories('.'),
  link_with: image_lib,
  dependencies: termpaint_dep
)
if meson.version().version_compare('>=0.54')
  meson.override_dependency('termpaint_image', termpaint_image_dep)
endif

tools_kwargs = {}

if get_option('tools-path') != ''
  tools_kwargs += {'install': true, 'install_dir': get_option('tools-path')}
endif

#ide:editable-filelist
inputevents_files = [
  'inputevents.cpp',
]

executable('inputevents', inputevents_files, link_with: [main_lib], kwargs: tools_kwargs)

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
shuffledemo_files = [
  'demo/shuffle.c',
]

executable('shuffle', shuffledemo_files, link_with: [main_lib])

#ide:editable-filelist
lifedemo_files = [
  'demo/life.c',
]

executable('life', lifedemo_files, link_with: [main_lib])

#ide:editable-filelist
textwrapdemo_files = [
  'demo/textwrap.c',
]

executable('textwrap', textwrapdemo_files, link_with: [main_lib])

#ide:editable-filelist
detect_files = [
  'demo/detect.c',
]

executable('detect', detect_files, link_with: [main_lib], kwargs: tools_kwargs)

#ide:editable-filelist
keyboardcollector_files = [
  'tools/keyboardcollector.cpp',
]


executable('keyboardcollector', keyboardcollector_files, link_with: [main_lib], dependencies: [picojson_dep])
if get_option('ssh')
  executable('keyboardcollector-ssh', 'tools/keyboardcollector.cpp', 'tools/SshServer.cpp',
    cpp_args: ['-DUSE_SSH'],
    link_with: [main_lib],
    dependencies: dependency('libssh'))
endif

docopt_dep = dependency('docopt', required : get_option('system-docopt'))
if not docopt_dep.found()
  docopt_lib = static_library('libdocopt', 'third-party/docopt/docopt.cpp', cpp_args: ['-Wno-unknown-pragmas'])
  docopt_dep = declare_dependency(link_with: docopt_lib, compile_args: ['-DBUNDLED_DOCOPT'])
endif

fmt_dep = dependency('fmt', required : get_option('system-fmt'))
if not fmt_dep.found()
  fmt_lib = static_library('libfmt', 'third-party/format.cc')
  fmt_dep = declare_dependency(link_with: fmt_lib, compile_args: ['-DBUNDLED_FMT'])
endif

executable('mcheck', 'tools/mcheck.cpp', link_with: [main_lib], dependencies: [docopt_dep, fmt_dep, picojson_dep])
executable('termquery', 'termquery.cpp', link_with: [main_lib])

catch2_dep = dependency('catch2', required : get_option('system-catch2'))
if not catch2_dep.found()
  catch2_dep = declare_dependency(compile_args: ['-DBUNDLED_CATCH2'])
else
  if catch2_dep.version().version_compare('>=3.0')
    catch2_dep = [catch2_dep, declare_dependency(compile_args: ['-DCATCH3'])]
  endif
endif

testlib = static_library('testlib', 'tests/catch_main.cpp', dependencies: [catch2_dep])

#ide:editable-filelist
test_files = [
  'tests/fingerprintingtests.cpp',
  'tests/hashtest.cpp',
  'tests/input_tests.cpp',
  'tests/measurement_tests.cpp',
  'tests/surface.cpp',
  'tests/terminal_misc.cpp',
  'tests/utf8_tests.cpp',
]

testtermpaint = executable('testtermpaint', test_files,
  link_with: [main_lib, testlib],
  cpp_args: ['-fno-inline', silence_warnings],
  dependencies: [catch2_dep, picojson_dep])

testtermpaint_env = environment()
testtermpaint_env.set('TERMPAINT_TEST_DATA', meson.current_source_dir() / ('tests'))
test('testtermpaint', testtermpaint, timeout: 1200, env: testtermpaint_env)

#ide:editable-filelist
test_terminaloutput_files = [
  'tests/terminaloutput.cpp',
  'tests/terminaloutput_main.cpp',
]

testtermpaint_terminaloutput = executable('testtermpaint_terminaloutput',
  test_terminaloutput_files,
  link_with: [main_lib],
  dependencies: [dependency('threads'), catch2_dep, picojson_dep],
  cpp_args: ['-fno-inline', silence_warnings])

# currently can't be run as meson registered test, because it needs a path to a terminal test driver
#test('testtermpaint_terminaloutput', testtermpaint_terminaloutput,
#  timeout: 300)

# doc snippets
executable('doc-getting-started', 'doc/getting-started.c', link_with: [main_lib])
executable('doc-sync-event-handling', 'doc/sync-event-handling.c', link_with: [main_lib])
executable('doc-callback-event-handling', 'doc/callback-event-handling.c', link_with: [main_lib])
