shaula_preflight_probe_c = static_library(
  'shaula-preflight-probe-c',
  'probe.c',
  include_directories: shaula_preflight_include,
  dependencies: [
    glib_dep,
    shaula_capabilities_runtime_dep,
    shaula_cli_json_dep,
    shaula_error_taxonomy_dep,
  ],
)

shaula_preflight_probe_dep = declare_dependency(
  link_with: shaula_preflight_probe_c,
  include_directories: shaula_preflight_include,
  dependencies: [
    glib_dep,
    shaula_capabilities_runtime_dep,
    shaula_cli_json_dep,
    shaula_error_taxonomy_dep,
  ],
)
