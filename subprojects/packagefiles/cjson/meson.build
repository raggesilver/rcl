project('cjson', 'c', version: '1.7.18')

cjson_lib = library(
  'cjson',
  'cJSON.c',
  include_directories: include_directories('.'),
  install: false,
)

libcjson_dep = declare_dependency(
  include_directories: include_directories('.'),
  link_with: cjson_lib,
)
