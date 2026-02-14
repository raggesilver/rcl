project(
  'rcl',
  'c',
  version: '0.2',
  default_options: [
    'warning_level=3',
    'c_std=c99',
    'c_args=-Wno-override-init -Wno-initializer-overrides -Wno-gnu-auto-type -Wno-pedantic',
  ],
  license: 'GPL-3.0',
)

# These arguments are only used to build the shared library
# not the executables that use the library.
lib_args = ['-DBUILDING_RCL']

sources = files(
  './src/array.c',
  './src/array.h',
  './src/hashtable.c',
  './src/hashtable.h',
  './src/string.c',
  './src/string.h',
)

# incs = include_directories(
#   './src',
# )

shlib = shared_library(
  'rcl',
  sources,
  # include_directories: incs,
  install: true,
  c_args: lib_args,
  # gnu_symbol_visibility: 'hidden',
)

# Make this library usable as a Meson subproject.
rcl_dep = declare_dependency(
  # include_directories: incs,
  link_with: shlib,
)

# Make this library usable from the system's
# package manager.
install_headers('./src/array.h', subdir: 'rcl')
install_headers('./src/hashtable.h', subdir: 'rcl')
install_headers('./src/string.h', subdir: 'rcl')

pkg_mod = import('pkgconfig')
pkg_mod.generate(
  name: 'rcl',
  filebase: 'rcl',
  description: 'Raggesilver\'s C Library.',
  subdirs: 'rcl',
  libraries: shlib,
  version: '0.1',
)

if get_option('tests')
  unity_subproject = subproject('unity')
  unity_dependency = unity_subproject.get_variable('unity_dep')

  test_exe = executable(
    'hashtable',
    'src' / 'hashtable_test.c',
    dependencies: [rcl_dep, unity_dependency],
  )
  test('hashtable', test_exe)

  array_test_exe = executable(
    'array',
    'src' / 'array_test.c',
    dependencies: [rcl_dep, unity_dependency],
  )
  test('array', array_test_exe)

  rcl_string_test_exe = executable(
    'string',
    'src' / 'string_test.c',
    dependencies: [rcl_dep, unity_dependency],
  )
  test('string', rcl_string_test_exe)
endif