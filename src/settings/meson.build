shaula_settings_config_c = static_library(
  'shaula-settings-config-c',
  'settings_config.c',
  include_directories: shaula_settings_include,
  dependencies: [glib_dep, shaula_config_dep, shaula_cli_json_dep],
  install: false,
)

shaula_settings_config_dep = declare_dependency(
  link_with: shaula_settings_config_c,
  include_directories: shaula_settings_include,
  dependencies: [glib_dep, shaula_config_dep, shaula_cli_json_dep],
)

shaula_settings_process_c = static_library(
  'shaula-settings-process-c',
  'settings_process.c',
  include_directories: shaula_settings_include,
  dependencies: [glib_dep, shaula_runtime_dep, shaula_settings_config_dep],
  install: false,
)

shaula_settings_process_dep = declare_dependency(
  link_with: shaula_settings_process_c,
  include_directories: shaula_settings_include,
  dependencies: [glib_dep, shaula_runtime_dep, shaula_settings_config_dep],
)

shaula_settings_shortcuts_c = static_library(
  'shaula-settings-shortcuts-c',
  'settings_shortcuts.c',
  include_directories: shaula_settings_include,
  dependencies: [glib_dep, shaula_shortcuts_dep],
  install: false,
)

shaula_settings_shortcuts_dep = declare_dependency(
  link_with: shaula_settings_shortcuts_c,
  include_directories: shaula_settings_include,
  dependencies: [glib_dep, shaula_shortcuts_dep],
)

shaula_settings_niri_c = static_library(
  'shaula-settings-niri-c',
  'settings_niri.c',
  include_directories: shaula_settings_include,
  dependencies: [glib_dep, json_glib_dep, shaula_settings_process_dep],
  install: false,
)

shaula_settings_niri_dep = declare_dependency(
  link_with: shaula_settings_niri_c,
  include_directories: shaula_settings_include,
  dependencies: [glib_dep, json_glib_dep, shaula_settings_process_dep],
)

executable(
  'shaula-settings',
  'native_gtk_settings.c',
  dependencies: [gtk_dep, shaula_runtime_dep, shaula_settings_config_dep,
                 shaula_settings_process_dep, shaula_settings_niri_dep,
                 shaula_settings_shortcuts_dep, shaula_shortcuts_dep],
  c_args: shaula_existing_helper_warning_args,
  install: true,
)

executable(
  'shaula-launcher',
  'native_gtk_launcher.c',
  dependencies: [gtk_dep, shaula_shortcuts_dep],
  c_args: shaula_existing_helper_warning_args,
  install: true,
)
