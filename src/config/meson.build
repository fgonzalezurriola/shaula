shaula_config_c = static_library(
  'shaula-config-c',
  ['config.c', 'niri_managed.c', 'noctalia_managed.c'],
  include_directories: include_directories('..'),
  dependencies: [glib_dep, json_glib_dep, shaula_runtime_dep],
)

shaula_config_dep = declare_dependency(
  link_with: shaula_config_c,
  include_directories: include_directories('..'),
  dependencies: [glib_dep, json_glib_dep, shaula_runtime_dep],
)
