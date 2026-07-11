shaula_error_taxonomy_c = static_library(
  'shaula-error-taxonomy-c',
  'taxonomy.c',
  include_directories: shaula_errors_include,
  install: false,
)

shaula_error_taxonomy_dep = declare_dependency(
  link_with: shaula_error_taxonomy_c,
  include_directories: shaula_errors_include,
)
