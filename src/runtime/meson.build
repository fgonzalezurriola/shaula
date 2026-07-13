shaula_runtime_c = static_library(
  'shaula-runtime-c',
  [
    'env.c',
    'paths.c',
    'tool_lookup.c',
    'helper_resolution.c',
    'previous_area_store.c',
    'capture_session_lock.c',
    'capture_state.c',
    'process_exec.c',
  ],
  include_directories: shaula_runtime_include,
  dependencies: glib_dep,
)

shaula_runtime_dep = declare_dependency(
  link_with: shaula_runtime_c,
  include_directories: shaula_runtime_include,
  dependencies: glib_dep,
)
