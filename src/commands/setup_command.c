#include "commands.h"

#include "config/config.h"
#include "config/niri_managed.h"
#include "config/noctalia_managed.h"
#include "config_command_support.h"
#include "support.h"

#include <fcntl.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  gboolean assume_yes;
  gboolean integrations;
  gboolean niri;
  gboolean noctalia;
  gboolean niri_explicit;
  gboolean noctalia_explicit;
  gboolean install_keybinds;
  gboolean remove;
  gboolean dry_run;
} SetupOptions;

static gboolean setup_can_prompt(void) {
  int fd = open("/dev/tty", O_RDWR);
  if (fd < 0)
    return FALSE;
  (void)close(fd);
  return TRUE;
}

static gboolean setup_prompt(const char *question) {
  FILE *terminal = fopen("/dev/tty", "r+");
  if (terminal == NULL)
    return FALSE;
  (void)fprintf(terminal, "%s [y/N] ", question);
  (void)fflush(terminal);
  char answer[32] = {0};
  gboolean accepted =
      fgets(answer, sizeof(answer), terminal) != NULL &&
      (g_ascii_strncasecmp(answer, "y", 1) == 0);
  (void)fclose(terminal);
  return accepted;
}

static void setup_status(const char *status, const char *subject,
                         const char *detail) {
  if (detail != NULL && detail[0] != '\0')
    g_print("%s: %s (%s)\n", status, subject, detail);
  else
    g_print("%s: %s\n", status, subject);
}

static gboolean parse_setup_options(int argc, char **argv,
                                    SetupOptions *options) {
  *options = (SetupOptions){
      .integrations = TRUE,
      .niri = TRUE,
      .noctalia = TRUE,
  };
  for (int i = 2; i < argc; i++) {
    if (g_str_equal(argv[i], "--help")) {
      g_print(
          "Usage: shaula setup [--yes] [--niri] [--noctalia] "
          "[--niri-keybinds] [--no-integrations] [--no-niri] "
          "[--no-noctalia] [--remove] [--dry-run]\n");
      return FALSE;
    }
    if (g_str_equal(argv[i], "--yes"))
      options->assume_yes = TRUE;
    else if (g_str_equal(argv[i], "--no-integrations"))
      options->integrations = FALSE;
    else if (g_str_equal(argv[i], "--niri"))
      options->niri_explicit = TRUE;
    else if (g_str_equal(argv[i], "--noctalia"))
      options->noctalia_explicit = TRUE;
    else if (g_str_equal(argv[i], "--no-niri"))
      options->niri = FALSE;
    else if (g_str_equal(argv[i], "--no-noctalia"))
      options->noctalia = FALSE;
    else if (g_str_equal(argv[i], "--niri-keybinds")) {
      options->install_keybinds = TRUE;
      options->niri_explicit = TRUE;
    } else if (g_str_equal(argv[i], "--remove"))
      options->remove = TRUE;
    else if (g_str_equal(argv[i], "--dry-run"))
      options->dry_run = TRUE;
    else
      return FALSE;
  }
  return TRUE;
}

static gboolean setup_niri_install(const ShaulaConfig *config,
                                   const SetupOptions *options,
                                   const char *path) {
  g_autofree char *rule = shaula_config_command_preview_rule(config);
  ManagedBlockResult result = {0};
  gboolean invalid = FALSE;
  if (!install_managed_block(path, "// BEGIN SHAULA PREVIEW WINDOW RULE",
                             "// END SHAULA PREVIEW WINDOW RULE", rule,
                             options->dry_run, &result, &invalid)) {
    setup_status("failed", "Niri preview rule",
                 invalid ? "invalid managed markers" : "write failed");
    managed_block_result_clear(&result);
    return FALSE;
  }
  setup_status(result.changed ? (options->dry_run ? "would-install"
                                                   : "installed")
                              : "unchanged",
               "Niri preview rule", result.path);
  managed_block_result_clear(&result);

  if (!options->install_keybinds) {
    setup_status("skipped", "Niri keybindings", "not requested");
    return TRUE;
  }
  g_autoptr(GPtrArray) conflicts = niri_keybind_conflicts(path);
  if (conflicts != NULL && conflicts->len > 0U) {
    setup_status("failed", "Niri keybindings", "conflicts detected");
    for (guint i = 0U; i < conflicts->len; i++)
      g_printerr("  %s\n", (const char *)g_ptr_array_index(conflicts, i));
    return FALSE;
  }
  g_autofree char *keybinds = shaula_config_command_render_keybinds();
  result = (ManagedBlockResult){0};
  invalid = FALSE;
  if (!install_managed_block(path, "// BEGIN SHAULA MANAGED KEYBINDS",
                             "// END SHAULA MANAGED KEYBINDS", keybinds,
                             options->dry_run, &result, &invalid)) {
    setup_status("failed", "Niri keybindings",
                 invalid ? "invalid managed markers" : "write failed");
    managed_block_result_clear(&result);
    return FALSE;
  }
  setup_status(result.changed ? (options->dry_run ? "would-install"
                                                   : "installed")
                              : "unchanged",
               "Niri keybindings", result.path);
  managed_block_result_clear(&result);
  return TRUE;
}

static gboolean setup_niri_remove(const SetupOptions *options,
                                  const char *path) {
  ManagedBlockResult result = {0};
  gboolean invalid = FALSE;
  if (!remove_managed_block(path, "// BEGIN SHAULA PREVIEW WINDOW RULE",
                            "// END SHAULA PREVIEW WINDOW RULE",
                            options->dry_run, &result, &invalid)) {
    setup_status("failed", "Niri preview rule removal",
                 invalid ? "invalid managed markers" : "write failed");
    managed_block_result_clear(&result);
    return FALSE;
  }
  setup_status(result.changed ? (options->dry_run ? "would-remove" : "removed")
                              : "unchanged",
               "Niri preview rule", result.path);
  managed_block_result_clear(&result);

  result = (ManagedBlockResult){0};
  invalid = FALSE;
  if (!remove_managed_keybinds(path, options->dry_run, &result, &invalid)) {
    setup_status("failed", "Niri keybindings removal",
                 invalid ? "invalid managed markers" : "write failed");
    managed_block_result_clear(&result);
    return FALSE;
  }
  setup_status(result.changed ? (options->dry_run ? "would-remove" : "removed")
                              : "unchanged",
               "Niri keybindings", result.path);
  managed_block_result_clear(&result);
  return TRUE;
}

static gboolean setup_noctalia(const SetupOptions *options) {
  ShaulaNoctaliaResult result = {0};
  ShaulaNoctaliaStatus status;
  if (options->remove) {
    status = shaula_noctalia_remove(options->dry_run, &result);
  } else {
    g_autofree char *source = shaula_noctalia_plugin_source_resolve();
    status = shaula_noctalia_install(source, options->dry_run, &result);
  }

  if (status == SHAULA_NOCTALIA_STATUS_NOT_DETECTED) {
    setup_status("skipped", "Noctalia integration", "not detected");
    shaula_noctalia_result_clear(&result);
    return TRUE;
  }
  if (status != SHAULA_NOCTALIA_STATUS_OK) {
    setup_status("failed", "Noctalia integration",
                 shaula_noctalia_status_token(status));
    shaula_noctalia_result_clear(&result);
    return FALSE;
  }
  setup_status(result.changed
                   ? (options->dry_run
                          ? (options->remove ? "would-remove" : "would-install")
                          : (options->remove ? "removed" : "installed"))
                   : "unchanged",
               "Noctalia integration", result.plugin_dir);
  if (result.plugins_json_skipped)
    setup_status("skipped", "Noctalia plugins.json", "file not present");
  if (result.settings_json_skipped)
    setup_status("skipped", "Noctalia settings.json", "file not present");
  shaula_noctalia_result_clear(&result);
  return TRUE;
}

int shaula_setup_command_run(int argc, char **argv) {
  SetupOptions options;
  if (!parse_setup_options(argc, argv, &options)) {
    if (argc > 2 && g_str_equal(argv[2], "--help"))
      return 0;
    return shaula_command_write_error("setup", "ERR_CLI_USAGE",
                                      "unsupported setup flag", "{}");
  }

  ShaulaConfig config;
  g_autofree char *path = shaula_config_path_new();
  gboolean loaded = FALSE;
  if (path == NULL ||
      shaula_config_load(path, &config, &loaded) != SHAULA_CONFIG_STATUS_OK ||
      (!loaded && !options.dry_run &&
       shaula_config_save(path, &config) != SHAULA_CONFIG_STATUS_OK))
    return shaula_command_write_error(
        "setup", "ERR_CONFIG_UNREADABLE",
        "configuration path could not be resolved", "{}");

  g_print("Shaula setup\n");
  setup_status(loaded ? "unchanged"
                      : (options.dry_run ? "would-install" : "installed"),
               "configuration", path);

  if (!options.integrations) {
    setup_status("skipped", "optional integrations", "disabled");
    setup_status("installed", "setup", "core configuration ready");
    return 0;
  }

  const gboolean interactive = setup_can_prompt();
  gboolean ok = TRUE;
  g_autofree char *niri_path = niri_config_path(NULL);
  const gboolean niri_detected =
      options.niri && niri_path != NULL &&
      g_file_test(niri_path, G_FILE_TEST_IS_REGULAR);
  gboolean use_niri = options.niri_explicit || options.assume_yes;
  if (niri_detected && !options.remove && !use_niri && interactive)
    use_niri = setup_prompt("Install Shaula's Niri window rule?");
  if (options.remove)
    use_niri = niri_detected;
  if (!niri_detected) {
    setup_status("skipped", "Niri integration", "not detected");
  } else if (!use_niri) {
    setup_status("skipped", "Niri integration",
                 interactive ? "declined" : "noninteractive");
  } else {
    ok = (options.remove ? setup_niri_remove(&options, niri_path)
                         : setup_niri_install(&config, &options, niri_path)) &&
         ok;
  }

  const gboolean noctalia_detected =
      options.noctalia && shaula_noctalia_detected();
  gboolean use_noctalia = options.noctalia_explicit || options.assume_yes;
  if (noctalia_detected && !options.remove && !use_noctalia && interactive)
    use_noctalia = setup_prompt("Install the Shaula Noctalia widget?");
  if (options.remove)
    use_noctalia = noctalia_detected;
  if (!noctalia_detected) {
    setup_status("skipped", "Noctalia integration", "not detected");
  } else if (!use_noctalia) {
    setup_status("skipped", "Noctalia integration",
                 interactive ? "declined" : "noninteractive");
  } else {
    ok = setup_noctalia(&options) && ok;
  }

  setup_status(ok ? "installed" : "failed", "setup",
               ok ? "complete" : "optional integration incomplete");
  return ok ? 0 : 1;
}
