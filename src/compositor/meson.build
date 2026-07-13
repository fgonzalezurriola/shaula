shaula_compositor_runtime_c = static_library(
  'shaula-compositor-runtime-c',
  'runtime.c',
  'focused_output.c',
  include_directories: shaula_compositor_include,
  dependencies: [glib_dep, shaula_runtime_dep],
)

shaula_compositor_runtime_dep = declare_dependency(
  link_with: shaula_compositor_runtime_c,
  include_directories: shaula_compositor_include,
  dependencies: [glib_dep, shaula_runtime_dep],
)
