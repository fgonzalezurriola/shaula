shaula_clipboard_c = static_library(
  'shaula-clipboard-c',
  'clipboard.c',
  include_directories: include_directories('..'),
  dependencies: [glib_dep, shaula_runtime_dep],
  install: false,
)

shaula_clipboard_dep = declare_dependency(
  link_with: shaula_clipboard_c,
  include_directories: include_directories('..'),
  dependencies: [glib_dep, shaula_runtime_dep],
)

shaula_clipboard_handoff_c = static_library(
  'shaula-clipboard-handoff-c',
  'provider_handoff.c',
  dependencies: [glib_dep, gio_dep],
  install: false,
)

shaula_clipboard_handoff_dep = declare_dependency(
  link_with: shaula_clipboard_handoff_c,
  include_directories: include_directories('.'),
  dependencies: [glib_dep, gio_dep],
)

executable(
  'shaula-clipboard-provider',
  'provider.c',
  dependencies: [gtk_dep, shaula_clipboard_handoff_dep],
  c_args: shaula_existing_helper_warning_args,
  install: true,
)
