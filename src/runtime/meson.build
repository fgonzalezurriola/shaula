shaula_runtime_env_c = static_library(
  'shaula-runtime-env-c',
  'env.c',
  include_directories: shaula_runtime_include,
)

shaula_runtime_env_dep = declare_dependency(
  link_with: shaula_runtime_env_c,
  include_directories: shaula_runtime_include,
)
