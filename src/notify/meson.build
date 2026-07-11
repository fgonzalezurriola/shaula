shaula_notify_request_c = static_library(
  'shaula-notify-request-c',
  'request.c',
  include_directories: shaula_notify_include,
  dependencies: glib_dep,
  install: false,
)

shaula_notify_request_dep = declare_dependency(
  link_with: shaula_notify_request_c,
  include_directories: shaula_notify_include,
  dependencies: glib_dep,
)
