cli_json_test = executable(
  'cli-json-test',
  'cli_json_test.c',
  dependencies: shaula_cli_json_dep,
)

test(
  'cli-json',
  cli_json_test,
  suite: ['unit'],
)

error_taxonomy_test = executable(
  'error-taxonomy-test',
  'error_taxonomy_test.c',
  dependencies: [shaula_error_taxonomy_dep, glib_dep],
)

error_taxonomy_test_env = environment()
error_taxonomy_test_env.set(
  'SHAULA_ERRORS_FIXTURE',
  join_paths(meson.project_source_root(), 'tests/fixtures/contracts/errors-list.json'),
)

test(
  'error-taxonomy',
  error_taxonomy_test,
  suite: ['unit'],
  env: error_taxonomy_test_env,
)

core_capture_mode_test = executable(
  'core-capture-mode-test',
  'core_capture_mode_test.c',
  dependencies: [shaula_core_capture_mode_dep, glib_dep],
)

test(
  'core-capture-mode',
  core_capture_mode_test,
  suite: ['unit'],
)

compositor_runtime_test = executable(
  'compositor-runtime-test',
  'compositor_runtime_test.c',
  dependencies: [shaula_compositor_runtime_dep, glib_dep],
)

test(
  'compositor-runtime',
  compositor_runtime_test,
  suite: ['unit'],
)

compositor_focused_output_test = executable(
  'compositor-focused-output-test',
  'compositor_focused_output_test.c',
  dependencies: [shaula_compositor_runtime_dep, glib_dep],
)

test(
  'compositor-focused-output',
  compositor_focused_output_test,
  suite: ['unit'],
)

capabilities_runtime_test = executable(
  'capabilities-runtime-test',
  'capabilities_runtime_test.c',
  dependencies: [shaula_capabilities_runtime_dep, glib_dep],
)

test(
  'capabilities-runtime',
  capabilities_runtime_test,
  suite: ['unit'],
)

preflight_probe_test = executable(
  'preflight-probe-test',
  'preflight_probe_test.c',
  dependencies: [shaula_preflight_probe_dep, glib_dep],
)

test(
  'preflight-probe',
  preflight_probe_test,
  suite: ['unit'],
)

notify_request_test = executable(
  'notify-request-test',
  'notify_request_test.c',
  dependencies: shaula_notify_request_dep,
)

test(
  'notify-request',
  notify_request_test,
  suite: ['unit'],
)

preview_result_test = executable(
  'preview-result-test',
  'preview_result_test.c',
  dependencies: shaula_preview_result_dep,
)

test(
  'preview-result',
  preview_result_test,
  suite: ['unit'],
)

preview_geometry_test = executable(
  'preview-geometry-test',
  'preview_geometry_test.c',
  dependencies: shaula_preview_geometry_dep,
)

test(
  'preview-geometry',
  preview_geometry_test,
  suite: ['unit'],
)

preview_image_io_test = executable(
  'preview-image-io-test',
  'preview_image_io_test.c',
  dependencies: shaula_preview_image_io_dep,
)

test(
  'preview-image-io',
  preview_image_io_test,
  suite: ['unit'],
)

preview_clipboard_test = executable(
  'preview-clipboard-test',
  'preview_clipboard_test.c',
  dependencies: shaula_preview_clipboard_dep,
)

test(
  'preview-clipboard',
  preview_clipboard_test,
  suite: ['unit'],
)

clipboard_lifecycle_test = executable(
  'clipboard-lifecycle-test',
  'clipboard_lifecycle_test.c',
  dependencies: [glib_dep, shaula_clipboard_dep],
)

test(
  'clipboard-lifecycle',
  clipboard_lifecycle_test,
  suite: ['unit'],
)

clipboard_handoff_test = executable(
  'clipboard-handoff-test',
  'clipboard_handoff_test.c',
  dependencies: [glib_dep, gio_dep, shaula_clipboard_handoff_dep],
)

test(
  'clipboard-handoff',
  clipboard_handoff_test,
  suite: ['unit'],
  timeout: 30,
)

preview_notify_test = executable(
  'preview-notify-test',
  'preview_notify_test.c',
  dependencies: shaula_preview_notify_dep,
)

test(
  'preview-notify',
  preview_notify_test,
  suite: ['unit'],
)

overlay_selection_session_test = executable(
  'overlay-selection-session-test',
  'overlay_selection_session_test.c',
  dependencies: shaula_overlay_selection_dep,
)

test(
  'overlay-selection-session',
  overlay_selection_session_test,
  suite: ['unit'],
)

settings_config_test = executable(
  'settings-config-test',
  'settings_config_test.c',
  dependencies: shaula_settings_config_dep,
)

test(
  'settings-config',
  settings_config_test,
  suite: ['unit'],
)

settings_process_test = executable(
  'settings-process-test',
  'settings_process_test.c',
  dependencies: shaula_settings_process_dep,
)

test(
  'settings-process',
  settings_process_test,
  suite: ['unit'],
)

noctalia_managed_test = executable(
  'noctalia-managed-test',
  'noctalia_managed_test.c',
  dependencies: [shaula_config_dep, json_glib_dep],
)

test(
  'noctalia-managed',
  noctalia_managed_test,
  suite: ['unit'],
)

runtime_env_test = executable(
  'runtime-env-test',
  'runtime_env_test.c',
  dependencies: [glib_dep, shaula_runtime_dep],
)

test(
  'runtime-env',
  runtime_env_test,
  suite: ['unit'],
)

runtime_paths_test = executable(
  'runtime-paths-test',
  'runtime_paths_test.c',
  dependencies: shaula_runtime_dep,
)

test(
  'runtime-paths',
  runtime_paths_test,
  suite: ['unit'],
)

runtime_tool_lookup_test = executable(
  'runtime-tool-lookup-test',
  'runtime_tool_lookup_test.c',
  dependencies: shaula_runtime_dep,
)

test(
  'runtime-tool-lookup',
  runtime_tool_lookup_test,
  suite: ['unit'],
)

runtime_helper_resolution_test = executable(
  'runtime-helper-resolution-test',
  'runtime_helper_resolution_test.c',
  dependencies: shaula_runtime_dep,
)

test(
  'runtime-helper-resolution',
  runtime_helper_resolution_test,
  suite: ['unit'],
)

runtime_previous_area_store_test = executable(
  'runtime-previous-area-store-test',
  'runtime_previous_area_store_test.c',
  dependencies: shaula_runtime_dep,
)

test(
  'runtime-previous-area-store',
  runtime_previous_area_store_test,
  suite: ['unit'],
)

runtime_capture_session_lock_test = executable(
  'runtime-capture-session-lock-test',
  'runtime_capture_session_lock_test.c',
  dependencies: shaula_runtime_dep,
)

test(
  'runtime-capture-session-lock',
  runtime_capture_session_lock_test,
  suite: ['unit'],
)

runtime_capture_state_test = executable(
  'runtime-capture-state-test',
  'runtime_capture_state_test.c',
  dependencies: shaula_runtime_dep,
)

test(
  'runtime-capture-state',
  runtime_capture_state_test,
  suite: ['unit'],
)

runtime_process_exec_test = executable(
  'runtime-process-exec-test',
  'runtime_process_exec_test.c',
  dependencies: shaula_runtime_dep,
)

test(
  'runtime-process-exec',
  runtime_process_exec_test,
  suite: ['unit'],
)

bash_program = find_program('bash')
test(
  'dev-noctalia-restart',
  bash_program,
  args: [files('dev_noctalia_test.sh')],
  suite: ['unit'],
)
