#include "config_command_support.h"

#include "support.h"

#include <glib.h>

char *shaula_config_command_preview_rule(const ShaulaConfig *config) {
  GString *rule = g_string_new(
      "window-rule {\n    match app-id=\"^dev\\\\.shaula\\\\.preview$\"\n");
  if (g_str_equal(config->preview_mode, "floating"))
    g_string_append(rule, "    open-floating true\n");
  else if (g_str_equal(config->preview_mode, "tiling"))
    g_string_append(rule, "    open-floating false\n");
  else if (g_str_equal(config->preview_mode, "maximized"))
    g_string_append(rule,
                    "    open-floating false\n    open-maximized true\n");
  else if (g_str_equal(config->preview_mode, "maximized-to-edges"))
    g_string_append(
        rule,
        "    open-floating false\n    open-maximized-to-edges true\n");
  else if (g_str_equal(config->preview_mode, "fullscreen"))
    g_string_append(rule,
                    "    open-floating false\n    open-fullscreen true\n");
  g_string_append_printf(
      rule,
      "    open-focused %s\n    default-column-width { fixed %u; }\n"
      "    default-window-height { fixed %u; }\n"
      "    default-column-display \"%s\"\n",
      shaula_command_json_bool(config->preview_focused), config->preview_width,
      config->preview_height, config->column_display);
  if (g_str_equal(config->preview_mode, "floating") &&
      config->floating_x_set && config->floating_y_set)
    g_string_append_printf(
        rule,
        "    default-floating-position x=%d y=%d relative-to=\"%s\"\n",
        config->floating_x, config->floating_y,
        config->floating_relative_to);
  g_string_append(rule, "}\n");
  return g_string_free(rule, FALSE);
}
