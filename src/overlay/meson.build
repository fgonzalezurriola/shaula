shaula_overlay_selection_c = static_library(
  'shaula-overlay-selection-c',
  ['overlay_selection.c', 'overlay_selection_session.c'],
  include_directories: shaula_overlay_include,
  dependencies: [glib_dep, math_dep],
  install: false,
)

shaula_overlay_selection_dep = declare_dependency(
  link_with: shaula_overlay_selection_c,
  include_directories: shaula_overlay_include,
  dependencies: [glib_dep, math_dep],
)

executable(
  'shaula-overlay',
  'native_gtk_overlay.c',
  dependencies: [gtk_dep, layer_shell_dep, math_dep,
                 shaula_overlay_selection_dep],
  c_args: ['-DSHAULA_OVERLAY_STANDALONE=1'] + shaula_existing_helper_warning_args,
  install: true,
)
