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
  dependencies: [glib_dep, gio_dep],
  install: false,
)

shaula_preview_clipboard_dep = declare_dependency(
  link_with: shaula_preview_clipboard_c,
  include_directories: shaula_preview_include,
  dependencies: [glib_dep, gio_dep],
)

shaula_preview_notify_c = static_library(
  'shaula-preview-notify-c',
  'preview_notify.c',
  include_directories: shaula_preview_include,
  dependencies: glib_dep,
  install: false,
)

shaula_preview_notify_dep = declare_dependency(
  link_with: shaula_preview_notify_c,
  include_directories: shaula_preview_include,
  dependencies: glib_dep,
)
