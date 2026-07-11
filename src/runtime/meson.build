shaula_runtime_env_c = static_library(
  'shaula-runtime-env-c',
  'env.c',
  include_directories: shaula_runtime_include,
)

shaula_runtime_env_dep = declare_dependency(
  link_with: shaula_runtime_env_c,
  include_directories: shaula_runtime_include,
)

shaula_runtime_paths_c = static_library(
  'shaula-runtime-paths-c',
  'paths.c',
  include_directories: shaula_runtime_include,
  dependencies: [glib_dep, shaula_runtime_env_dep],
)

shaula_runtime_paths_dep = declare_dependency(
  link_with: shaula_runtime_paths_c,
  include_directories: shaula_runtime_include,
  dependencies: [glib_dep, shaula_runtime_env_dep],
)

shaula_runtime_tool_lookup_c = static_library(
  'shaula-runtime-tool-lookup-c',
  'tool_lookup.c',
  include_directories: shaula_runtime_include,
  dependencies: glib_dep,
)

shaula_runtime_tool_lookup_dep = declare_dependency(
  link_with: shaula_runtime_tool_lookup_c,
  include_directories: shaula_runtime_include,
  dependencies: glib_dep,
)

shaula_runtime_helper_resolution_c = static_library(
  'shaula-runtime-helper-resolution-c',
  'helper_resolution.c',
  include_directories: shaula_runtime_include,
  dependencies: [glib_dep, shaula_runtime_env_dep, shaula_runtime_tool_lookup_dep],
)

shaula_runtime_helper_resolution_dep = declare_dependency(
  link_with: shaula_runtime_helper_resolution_c,
  include_directories: shaula_runtime_include,
  dependencies: [glib_dep, shaula_runtime_env_dep, shaula_runtime_tool_lookup_dep],
)

shaula_runtime_previous_area_store_c = static_library(
  'shaula-runtime-previous-area-store-c',
  'previous_area_store.c',
  include_directories: shaula_runtime_include,
  dependencies: [glib_dep, shaula_runtime_paths_dep],
)

shaula_runtime_previous_area_store_dep = declare_dependency(
  link_with: shaula_runtime_previous_area_store_c,
  include_directories: shaula_runtime_include,
  dependencies: [glib_dep, shaula_runtime_paths_dep],
)

shaula_runtime_capture_session_lock_c = static_library(
  'shaula-runtime-capture-session-lock-c',
  'capture_session_lock.c',
  include_directories: shaula_runtime_include,
  dependencies: [glib_dep, shaula_runtime_paths_dep],
)

shaula_runtime_capture_session_lock_dep = declare_dependency(
  link_with: shaula_runtime_capture_session_lock_c,
  include_directories: shaula_runtime_include,
  dependencies: [glib_dep, shaula_runtime_paths_dep],
)

shaula_runtime_process_exec_c = static_library(
  'shaula-runtime-process-exec-c',
  'process_exec.c',
  include_directories: shaula_runtime_include,
  dependencies: glib_dep,
)

shaula_runtime_process_exec_dep = declare_dependency(
  link_with: shaula_runtime_process_exec_c,
  include_directories: shaula_runtime_include,
  dependencies: glib_dep,
)
