shaula_shortcuts_c = static_library(
  'shaula-shortcuts-c',
  [
    'shortcuts.c',
    'state.c',
    'portal_adapter.c',
    'provider_core.c',
  ],
  include_directories: include_directories('..'),
  dependencies: [glib_dep, gio_dep, shaula_config_dep, shaula_runtime_dep],
)

shaula_shortcuts_dep = declare_dependency(
  link_with: shaula_shortcuts_c,
  include_directories: include_directories('..'),
  dependencies: [glib_dep, gio_dep, shaula_config_dep, shaula_runtime_dep],
)

executable(
  'shaula-shortcut-provider',
  'provider.c',
  include_directories: include_directories('..'),
  dependencies: [glib_dep, gio_dep, shaula_shortcuts_dep, shaula_runtime_dep],
  install: true,
)
