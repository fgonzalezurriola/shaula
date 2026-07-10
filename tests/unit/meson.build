preview_geometry_test = executable(
  'preview-geometry-test',
  'preview_geometry_test.c',
  dependencies: shaula_preview_geometry_dep,
)

test(
  'preview-geometry-c',
  preview_geometry_test,
  suite: ['port', 'unit'],
)

preview_image_io_test = executable(
  'preview-image-io-test',
  'preview_image_io_test.c',
  dependencies: shaula_preview_image_io_dep,
)

test(
  'preview-image-io-c',
  preview_image_io_test,
  suite: ['port', 'unit'],
)

preview_clipboard_test = executable(
  'preview-clipboard-test',
  'preview_clipboard_test.c',
  dependencies: shaula_preview_clipboard_dep,
)

test(
  'preview-clipboard-c',
  preview_clipboard_test,
  suite: ['port', 'unit'],
)

preview_notify_test = executable(
  'preview-notify-test',
  'preview_notify_test.c',
  dependencies: shaula_preview_notify_dep,
)

test(
  'preview-notify-c',
  preview_notify_test,
  suite: ['port', 'unit'],
)

settings_config_test = executable(
  'settings-config-test',
  'settings_config_test.c',
  dependencies: shaula_settings_config_dep,
)

test(
  'settings-config-c',
  settings_config_test,
  suite: ['port', 'unit'],
)

settings_process_test = executable(
  'settings-process-test',
  'settings_process_test.c',
  dependencies: shaula_settings_process_dep,
)

test(
  'settings-process-c',
  settings_process_test,
  suite: ['port', 'unit'],
)

runtime_env_test = executable(
  'runtime-env-test',
  'runtime_env_test.c',
  dependencies: [glib_dep, shaula_runtime_env_dep],
)

test(
  'runtime-env-c',
  runtime_env_test,
  suite: ['port', 'unit'],
)

bash_program = find_program('bash')
test(
  'dev-noctalia-restart',
  bash_program,
  args: [files('dev_noctalia_test.sh')],
  suite: ['port', 'unit'],
)
