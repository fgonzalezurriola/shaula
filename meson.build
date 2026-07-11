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
subdir('src/settings')

if get_option('port_tests')
  subdir('tests/unit')
endif

summary(
  {
    'Authoritative production build': 'Zig during migration',
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
