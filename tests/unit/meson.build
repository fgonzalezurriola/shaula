cli_json_test = executable(
  'cli-json-test',
  'cli_json_test.c',
  dependencies: shaula_cli_json_dep,
)

test(
  'cli-json-c',
  cli_json_test,
  suite: ['port', 'unit'],
)

error_taxonomy_test = executable(
  'error-taxonomy-test',
  'error_taxonomy_test.c',
  dependencies: [shaula_error_taxonomy_dep, glib_dep],
)

error_taxonomy_test_env = environment()
error_taxonomy_test_env.set(
  'SHAULA_ERRORS_FIXTURE',
  join_paths(meson.project_source_root(), 'tests/fixtures/port/errors-list.json'),
)

test(
  'error-taxonomy-c',
  error_taxonomy_test,
  suite: ['port', 'unit'],
  env: error_taxonomy_test_env,
)

core_capture_mode_test = executable(
  'core-capture-mode-test',
  'core_capture_mode_test.c',
  dependencies: [shaula_core_capture_mode_dep, glib_dep],
)

test(
  'core-capture-mode-c',
  core_capture_mode_test,
  suite: ['port', 'unit'],
)

compositor_runtime_test = executable(
  'compositor-runtime-test',
  'compositor_runtime_test.c',
  dependencies: [shaula_compositor_runtime_dep, glib_dep],
)

test(
  'compositor-runtime-c',
  compositor_runtime_test,
  suite: ['port', 'unit'],
)

compositor_focused_output_test = executable(
  'compositor-focused-output-test',
  'compositor_focused_output_test.c',
  dependencies: [shaula_compositor_runtime_dep, glib_dep],
)

test(
  'compositor-focused-output-c',
  compositor_focused_output_test,
  suite: ['port', 'unit'],
)

capabilities_runtime_test = executable(
  'capabilities-runtime-test',
  'capabilities_runtime_test.c',
  dependencies: [shaula_capabilities_runtime_dep, glib_dep],
)

test(
  'capabilities-runtime-c',
  capabilities_runtime_test,
  suite: ['port', 'unit'],
)

preflight_probe_test = executable(
  'preflight-probe-test',
  'preflight_probe_test.c',
  dependencies: [shaula_preflight_probe_dep, glib_dep],
)

test(
  'preflight-probe-c',
  preflight_probe_test,
  suite: ['port', 'unit'],
)

notify_request_test = executable(
  'notify-request-test',
  'notify_request_test.c',
  dependencies: shaula_notify_request_dep,
)

test(
  'notify-request-c',
  notify_request_test,
  suite: ['port', 'unit'],
)

preview_result_test = executable(
  'preview-result-test',
  'preview_result_test.c',
  dependencies: shaula_preview_result_dep,
)

test(
  'preview-result-c',
  preview_result_test,
  suite: ['port', 'unit'],
)

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
  dependencies: [glib_dep, shaula_runtime_dep],
)

test(
  'runtime-env-c',
  runtime_env_test,
  suite: ['port', 'unit'],
)

runtime_paths_test = executable(
  'runtime-paths-test',
  'runtime_paths_test.c',
  dependencies: shaula_runtime_dep,
)

test(
  'runtime-paths-c',
  runtime_paths_test,
  suite: ['port', 'unit'],
)

runtime_tool_lookup_test = executable(
  'runtime-tool-lookup-test',
  'runtime_tool_lookup_test.c',
  dependencies: shaula_runtime_dep,
)

test(
  'runtime-tool-lookup-c',
  runtime_tool_lookup_test,
  suite: ['port', 'unit'],
)

runtime_helper_resolution_test = executable(
  'runtime-helper-resolution-test',
  'runtime_helper_resolution_test.c',
  dependencies: shaula_runtime_dep,
)

test(
  'runtime-helper-resolution-c',
  runtime_helper_resolution_test,
  suite: ['port', 'unit'],
)

runtime_previous_area_store_test = executable(
  'runtime-previous-area-store-test',
  'runtime_previous_area_store_test.c',
  dependencies: shaula_runtime_dep,
)

test(
  'runtime-previous-area-store-c',
  runtime_previous_area_store_test,
  suite: ['port', 'unit'],
)

runtime_capture_session_lock_test = executable(
  'runtime-capture-session-lock-test',
  'runtime_capture_session_lock_test.c',
  dependencies: shaula_runtime_dep,
)

test(
  'runtime-capture-session-lock-c',
  runtime_capture_session_lock_test,
  suite: ['port', 'unit'],
)

runtime_capture_state_test = executable(
  'runtime-capture-state-test',
  'runtime_capture_state_test.c',
  dependencies: shaula_runtime_dep,
)

test(
  'runtime-capture-state-c',
  runtime_capture_state_test,
  suite: ['port', 'unit'],
)

runtime_process_exec_test = executable(
  'runtime-process-exec-test',
  'runtime_process_exec_test.c',
  dependencies: shaula_runtime_dep,
)

test(
  'runtime-process-exec-c',
  runtime_process_exec_test,
  suite: ['port', 'unit'],
)

bash_program = find_program('bash')
test(
  'dev-noctalia-restart',
  bash_program,
  args: [files('dev_noctalia_test.sh')],
  suite: ['port', 'unit'],
)
