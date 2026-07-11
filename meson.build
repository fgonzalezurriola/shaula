project(
  'shaula',
  'c',
  version: '0.1.5',
  meson_version: '>=1.3.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
)

cc = meson.get_compiler('c')

shaula_warning_args = cc.get_supported_arguments(
  '-Wpedantic',
  '-Wstrict-prototypes',
  '-Wmissing-prototypes',
  '-Wconversion',
  '-Wsign-conversion',
  '-Wshadow',
)
add_project_arguments(shaula_warning_args, language: 'c')

# Existing GTK helpers predate the port's strict leaf-module warning policy.
# Keep their historical warning surface while Meson assumes build ownership.
shaula_existing_helper_warning_args = [
  '-Wno-deprecated-declarations',
  '-Wno-pedantic',
  '-Wno-conversion',
  '-Wno-sign-conversion',
]

glib_dep = dependency('glib-2.0')
gio_dep = dependency('gio-2.0')
gtk_dep = dependency('gtk4')
gdk_pixbuf_dep = dependency('gdk-pixbuf-2.0')
cairo_dep = dependency('cairo')
pango_cairo_dep = dependency('pangocairo')
layer_shell_dep = dependency('gtk4-layer-shell-0')
math_dep = cc.find_library('m', required: true)

shaula_capabilities_include = include_directories('src')
shaula_cli_include = include_directories('src')
shaula_compositor_include = include_directories('src')
shaula_core_include = include_directories('src/core')
shaula_errors_include = include_directories('src')
shaula_notify_include = include_directories('src')
shaula_preflight_include = include_directories('src')
shaula_preview_include = include_directories('src/preview')
shaula_runtime_include = include_directories('src/runtime')
shaula_settings_include = include_directories('src/settings')

subdir('src/cli')
subdir('src/core')
subdir('src/errors')
subdir('src/notify')
subdir('src/preview')
subdir('src/runtime')
subdir('src/compositor')
subdir('src/capabilities')
subdir('src/preflight')
subdir('src/config')
subdir('src/settings')
subdir('src/overlay')
subdir('src/capture')

executable(
  'shaula',
  'src/main.c',
  dependencies: [
    glib_dep,
    gio_dep,
    gdk_pixbuf_dep,
    shaula_capabilities_runtime_dep,
    shaula_cli_json_dep,
    shaula_config_dep,
    shaula_error_taxonomy_dep,
    shaula_preflight_probe_dep,
    shaula_preview_result_dep,
    shaula_runtime_process_exec_dep,
    shaula_capture_command_dep,
  ],
  install: true,
)

install_subdir(
  'src/preview/icons/hicolor',
  install_dir: get_option('datadir') / 'icons',
)
install_subdir(
  'integrations/noctalia/shaula',
  install_dir: get_option('datadir') / 'shaula' / 'integrations' / 'noctalia',
)

if get_option('port_tests')
  subdir('tests/unit')
  subdir('scripts/qa')
endif

summary(
  {
    'Production helpers': 'Meson',
    'C port tests': get_option('port_tests'),
    'GLib': glib_dep.version(),
    'GIO': gio_dep.version(),
    'GTK': gtk_dep.version(),
    'GDK Pixbuf': gdk_pixbuf_dep.version(),
    'Cairo': cairo_dep.version(),
    'Pango/Cairo': pango_cairo_dep.version(),
    'GTK4 layer shell': layer_shell_dep.version(),
  },
  section: 'Shaula port foundation',
  bool_yn: true,
)
