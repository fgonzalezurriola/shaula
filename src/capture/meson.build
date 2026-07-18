executable(
  'shaula-crop-image',
  'backends/native_crop_image.c',
  dependencies: [gdk_pixbuf_dep, math_dep],
  c_args: shaula_existing_helper_warning_args,
  install: true,
)

shaula_capture_command_c = static_library(
  'shaula-capture-command-c',
  'command.c',
  include_directories: include_directories('..'),
  dependencies: [
    gdk_pixbuf_dep,
    gio_dep,
    shaula_capabilities_runtime_dep,
    shaula_clipboard_dep,
    shaula_cli_json_dep,
    shaula_compositor_runtime_dep,
    shaula_config_dep,
    shaula_error_taxonomy_dep,
    shaula_overlay_protocol_dep,
    shaula_preview_result_dep,
    shaula_runtime_dep,
  ],
)

shaula_capture_command_dep = declare_dependency(
  link_with: shaula_capture_command_c,
  include_directories: include_directories('..'),
  dependencies: [
    gdk_pixbuf_dep,
    gio_dep,
    shaula_capabilities_runtime_dep,
    shaula_clipboard_dep,
    shaula_cli_json_dep,
    shaula_compositor_runtime_dep,
    shaula_config_dep,
    shaula_error_taxonomy_dep,
    shaula_overlay_protocol_dep,
    shaula_preview_result_dep,
    shaula_runtime_dep,
  ],
)

executable(
  'shaula-portal-screenshot',
  'backends/native_portal_screenshot.c',
  dependencies: gio_dep,
  c_args: shaula_existing_helper_warning_args,
  install: true,
)
