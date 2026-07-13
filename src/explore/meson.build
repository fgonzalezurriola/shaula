shaula_explore_inventory_c = static_library(
  'shaula-explore-inventory-c',
  'inventory.c',
  include_directories: shaula_explore_include,
  dependencies: [glib_dep, json_glib_dep, shaula_runtime_dep],
)

shaula_explore_inventory_dep = declare_dependency(
  link_with: shaula_explore_inventory_c,
  include_directories: shaula_explore_include,
  dependencies: [glib_dep, json_glib_dep, shaula_runtime_dep],
)
