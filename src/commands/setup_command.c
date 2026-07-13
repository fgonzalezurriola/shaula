#include "commands.h"

#include "config/config.h"
#include "config/niri_managed.h"
#include "config_command_support.h"
#include "support.h"

#include <glib.h>

int shaula_setup_command_run(int argc, char **argv) {
  gboolean integrations = TRUE;
  gboolean niri = TRUE;
  gboolean install_keybinds = FALSE;
  gboolean skip_keybinds = FALSE;
  gboolean dry_run = FALSE;
  for (int i = 2; i < argc; i++) {
    if (g_str_equal(argv[i], "--help")) {
      g_print("Usage: shaula setup [--yes] [--no-integrations] [--no-niri] "
              "[--no-noctalia] [--niri-keybinds] [--skip-niri-keybinds] "
              "[--force] [--dry-run]\n");
      return 0;
    }
    if (g_str_equal(argv[i], "--no-integrations"))
      integrations = FALSE;
    else if (g_str_equal(argv[i], "--no-niri"))
      niri = FALSE;
    else if (g_str_equal(argv[i], "--niri-keybinds") ||
             g_str_equal(argv[i], "--force"))
      install_keybinds = TRUE;
    else if (g_str_equal(argv[i], "--skip-niri-keybinds"))
      skip_keybinds = TRUE;
    else if (g_str_equal(argv[i], "--dry-run"))
      dry_run = TRUE;
    else if (!g_str_equal(argv[i], "--yes") &&
             !g_str_equal(argv[i], "--no-noctalia"))
      return shaula_command_write_error(
          "setup", "ERR_CLI_USAGE", "unsupported setup flag", "{}");
  }

  ShaulaConfig config;
  g_autofree char *path = shaula_config_path_new();
  gboolean loaded = FALSE;
  if (path == NULL ||
      shaula_config_load(path, &config, &loaded) != SHAULA_CONFIG_STATUS_OK ||
      (!loaded && !dry_run &&
       shaula_config_save(path, &config) != SHAULA_CONFIG_STATUS_OK))
    return shaula_command_write_error(
        "setup", "ERR_CONFIG_UNREADABLE",
        "configuration path could not be resolved", "{}");

  g_print("Shaula setup\n\nok: %s config %s\n",
          loaded ? "kept" : dry_run ? "would create" : "created", path);

  g_autofree char *niri_path = niri_config_path(NULL);
  if (integrations && niri && niri_path != NULL &&
      g_file_test(niri_path, G_FILE_TEST_EXISTS)) {
    g_autofree char *rule = shaula_config_command_preview_rule(&config);
    ManagedBlockResult result = {0};
    gboolean invalid = FALSE;
    if (install_managed_block(niri_path,
                              "// BEGIN SHAULA PREVIEW WINDOW RULE",
                              "// END SHAULA PREVIEW WINDOW RULE", rule,
                              dry_run, &result, &invalid))
      g_print("ok: %s Niri preview rule in %s\n",
              result.changed ? dry_run ? "would install" : "installed"
                             : "kept",
              result.path);
    else
      g_printerr("warning: skipped Niri preview rule (%s)\n",
                 invalid ? "invalid managed block" : "unreadable config");
    managed_block_result_clear(&result);

    if (install_keybinds && !skip_keybinds) {
      g_autofree char *keybinds = shaula_config_command_render_keybinds();
      result = (ManagedBlockResult){0};
      invalid = FALSE;
      if (install_managed_block(niri_path,
                                "// BEGIN SHAULA MANAGED KEYBINDS",
                                "// END SHAULA MANAGED KEYBINDS", keybinds,
                                dry_run, &result, &invalid))
        g_print("ok: %s Niri keybinds in %s\n",
                result.changed ? dry_run ? "would install" : "installed"
                               : "kept",
                result.path);
      else
        g_printerr("warning: skipped Niri keybinds (%s)\n",
                   invalid ? "invalid managed block" : "unreadable config");
      managed_block_result_clear(&result);
    }
  } else if (integrations && niri) {
    g_print("info: Niri config was not detected; skipped Niri integration.\n");
  }

  g_print("\nok: setup complete\n");
  return 0;
}
