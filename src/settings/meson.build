shaula_settings_config_c = static_library(
  'shaula-settings-config-c',
  'settings_config.c',
  include_directories: shaula_settings_include,
  dependencies: glib_dep,
  install: false,
)

shaula_settings_config_dep = declare_dependency(
  link_with: shaula_settings_config_c,
  include_directories: shaula_settings_include,
  dependencies: glib_dep,
)

shaula_settings_process_c = static_library(
  'shaula-settings-process-c',
  'settings_process.c',
  include_directories: shaula_settings_include,
  dependencies: [glib_dep, shaula_settings_config_dep],
  install: false,
)

shaula_settings_process_dep = declare_dependency(
  link_with: shaula_settings_process_c,
  include_directories: shaula_settings_include,
  dependencies: [glib_dep, shaula_settings_config_dep],
)

executable(
  'shaula-settings',
  'native_gtk_settings.c',
  'settings_config.c',
  'settings_process.c',
  dependencies: gtk_dep,
  c_args: shaula_existing_helper_warning_args,
  install: true,
)
