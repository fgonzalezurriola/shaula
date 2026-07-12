executable(
  'shaula-overlay',
  ['native_gtk_overlay.c', 'overlay_selection.c'],
  dependencies: [gtk_dep, layer_shell_dep, math_dep],
  c_args: [
    '-DSHAULA_OVERLAY_STANDALONE',
    '-Wno-strict-prototypes',
    '-Wno-missing-prototypes',
  ] + shaula_existing_helper_warning_args,
  install: true,
)
