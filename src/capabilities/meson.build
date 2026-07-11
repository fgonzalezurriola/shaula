shaula_capabilities_runtime_c = static_library(
  'shaula-capabilities-runtime-c',
  'runtime.c',
  include_directories: shaula_capabilities_include,
  dependencies: [
    glib_dep,
    shaula_compositor_runtime_dep,
    shaula_core_capture_mode_dep,
    shaula_runtime_env_dep,
    shaula_runtime_process_exec_dep,
    shaula_runtime_tool_lookup_dep,
  ],
)

shaula_capabilities_runtime_dep = declare_dependency(
  link_with: shaula_capabilities_runtime_c,
  include_directories: shaula_capabilities_include,
  dependencies: [
    glib_dep,
    shaula_compositor_runtime_dep,
    shaula_core_capture_mode_dep,
    shaula_runtime_env_dep,
    shaula_runtime_process_exec_dep,
    shaula_runtime_tool_lookup_dep,
  ],
)
