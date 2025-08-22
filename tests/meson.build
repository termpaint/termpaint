# SPDX-License-Identifier: BSL-1.0

catch2_dep = dependency('catch2', required : get_option('system-catch2'))
if not catch2_dep.found()
  catch2_dep = declare_dependency(compile_args: ['-DBUNDLED_CATCH2'])
else
  if catch2_dep.version().version_compare('>=3.0')
    catch2_dep = [catch2_dep, declare_dependency(compile_args: ['-DCATCH3'])]
  endif
endif

testlib = static_library('testlib', 'catch_main.cpp', dependencies: [catch2_dep])

#ide:editable-filelist
test_files = [
  'fingerprintingtests.cpp',
  'hashtest.cpp',
  'input_tests.cpp',
  'measurement_tests.cpp',
  'surface.cpp',
  'terminal_misc.cpp',
  'utf8_tests.cpp',
]

testtermpaint = executable('testtermpaint', test_files,
  link_with: [testlib],
  cpp_args: ['-fno-inline', silence_warnings],
  dependencies: [catch2_dep, picojson_dep, termpaint_dep])

testtermpaint_env = environment()
testtermpaint_env.set('TERMPAINT_TEST_DATA', meson.current_source_dir())
test('testtermpaint', testtermpaint, timeout: 1200, env: testtermpaint_env)

#ide:editable-filelist
test_terminaloutput_files = [
  'terminaloutput.cpp',
  'terminaloutput_main.cpp',
]

testtermpaint_terminaloutput = executable('testtermpaint_terminaloutput',
  test_terminaloutput_files,
  dependencies: [dependency('threads'), catch2_dep, picojson_dep, termpaint_dep],
  cpp_args: ['-fno-inline', silence_warnings])

# currently can't be run as meson registered test, because it needs a path to a terminal test driver
#test('testtermpaint_terminaloutput', testtermpaint_terminaloutput,
#  timeout: 300)

