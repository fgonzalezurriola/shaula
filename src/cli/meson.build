shaula_cli_json_c = static_library(
  'shaula-cli-json-c',
  'json.c',
  include_directories: shaula_cli_include,
  dependencies: glib_dep,
  install: false,
)

shaula_cli_json_dep = declare_dependency(
  link_with: shaula_cli_json_c,
  include_directories: shaula_cli_include,
  dependencies: glib_dep,
)
