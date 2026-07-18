shaula_preview_result_c = static_library(
  'shaula-preview-result-c',
  'preview_result.c',
  include_directories: shaula_preview_include,
  dependencies: glib_dep,
  install: false,
)

shaula_preview_result_dep = declare_dependency(
  link_with: shaula_preview_result_c,
  include_directories: shaula_preview_include,
  dependencies: glib_dep,
)

shaula_preview_geometry_c = static_library(
  'shaula-preview-geometry-c',
  'preview_geometry.c',
  include_directories: shaula_preview_include,
  dependencies: [glib_dep, math_dep],
  install: false,
)

shaula_preview_geometry_dep = declare_dependency(
  link_with: shaula_preview_geometry_c,
  include_directories: shaula_preview_include,
  dependencies: [glib_dep, math_dep],
)

shaula_preview_image_io_c = static_library(
  'shaula-preview-image-io-c',
  'preview_image_io.c',
  include_directories: shaula_preview_include,
  dependencies: glib_dep,
  install: false,
)

shaula_preview_image_io_dep = declare_dependency(
  link_with: shaula_preview_image_io_c,
  include_directories: shaula_preview_include,
  dependencies: glib_dep,
)

shaula_preview_clipboard_c = static_library(
  'shaula-preview-clipboard-c',
  'preview_clipboard.c',
  include_directories: shaula_preview_include,
  dependencies: [glib_dep, shaula_clipboard_dep],
  install: false,
)

shaula_preview_clipboard_dep = declare_dependency(
  link_with: shaula_preview_clipboard_c,
  include_directories: shaula_preview_include,
  dependencies: [glib_dep, shaula_clipboard_dep],
)

shaula_preview_notify_c = static_library(
  'shaula-preview-notify-c',
  'preview_notify.c',
  include_directories: shaula_preview_include,
  dependencies: [glib_dep, shaula_runtime_dep],
  install: false,
)

shaula_preview_notify_dep = declare_dependency(
  link_with: shaula_preview_notify_c,
  include_directories: shaula_preview_include,
  dependencies: [glib_dep, shaula_runtime_dep],
)

shaula_preview_sources = files(
  'native_gtk_preview.c',
  'preview_actions.c',
  'preview_clipboard.c',
  'preview_image_io.c',
  'preview_notify.c',
  'preview_action_callbacks.c',
  'preview_annotation_editor.c',
  'preview_annotations.c',
  'preview_canvas.c',
  'preview_commands.c',
  'preview_document.c',
  'preview_edit_session.c',
  'preview_gesture.c',
  'preview_geometry.c',
  'preview_icons.c',
  'preview_measure.c',
  'preview_paths.c',
  'preview_paste_placement.c',
  'preview_properties_hud.c',
  'preview_properties_panel.c',
  'preview_tool_defaults.c',
  'preview_render.c',
  'preview_spotlight.c',
  'preview_state.c',
  'preview_system_clipboard.c',
  'preview_toolbar.c',
)

executable(
  'shaula-preview',
  shaula_preview_sources,
  dependencies: [gtk_dep, math_dep, shaula_clipboard_dep, shaula_runtime_dep],
  c_args: shaula_existing_helper_warning_args,
  install: true,
)
