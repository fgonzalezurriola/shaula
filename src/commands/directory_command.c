#include "commands.h"

#include "config/config.h"
#include "runtime/process_exec.h"
#include "support.h"

#include <glib.h>

static char *saved_directory(const ShaulaConfig *config) {
  const char *home = g_getenv("HOME");
  if (home == NULL || home[0] == '\0')
    return NULL;
  if (g_str_equal(config->save_folder, "~"))
    return g_strdup(home);
  if (g_str_has_prefix(config->save_folder, "~/"))
    return g_build_filename(home, config->save_folder + 2, NULL);
  if (g_path_is_absolute(config->save_folder))
    return g_strdup(config->save_folder);
  return NULL;
}

int shaula_directory_command_run(int argc, char **argv) {
  if (argc < 3 || !g_str_equal(argv[2], "screenshots"))
    return shaula_command_write_error(
        "directory", "ERR_CLI_USAGE",
        "usage: shaula directory screenshots [--open] [--json]", "{}");

  gboolean open = FALSE;
  gboolean json = FALSE;
  for (int i = 3; i < argc; i++) {
    if (g_str_equal(argv[i], "--open"))
      open = TRUE;
    else if (g_str_equal(argv[i], "--json"))
      json = TRUE;
    else
      return shaula_command_write_error("directory screenshots",
                                        "ERR_CLI_USAGE", "unsupported flag",
                                        "{}");
  }

  ShaulaConfig config;
  g_autofree char *config_path = shaula_config_path_new();
  (void)shaula_config_load(config_path, &config, NULL);
  g_autofree char *directory = saved_directory(&config);
  if (directory == NULL || g_mkdir_with_parents(directory, 0755) != 0)
    return shaula_command_write_error(
        "directory screenshots", "ERR_OUTPUT_PATH_INVALID",
        "screenshot directory is not writable", "{}");

  if (open) {
    char *open_argv[] = {"xdg-open", directory, NULL};
    int exit_code = 0;
    if (shaula_process_run_sync((const char *const *)open_argv, NULL, NULL,
                                NULL, &exit_code) !=
            SHAULA_PROCESS_STATUS_OK ||
        exit_code != 0)
      return shaula_command_write_error(
          "directory screenshots", "ERR_OUTPUT_PATH_INVALID",
          "could not open screenshot directory", "{}");
  }

  if (!json)
    return 0;
  g_autofree char *path_json = shaula_command_json_string(directory);
  g_autofree char *result =
      g_strdup_printf("{\"target\":\"screenshots\",\"path\":%s,"
                      "\"opened\":%s}",
                      path_json, shaula_command_json_bool(open));
  return shaula_command_write_success("directory screenshots", result, "[]");
}
