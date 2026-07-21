#include "commands.h"

#include "config/config.h"
#include "config/niri_managed.h"
#include "config/noctalia_managed.h"
#include "config_command_support.h"
#include "shortcuts/shortcuts.h"
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
  gboolean shortcuts;
  gboolean shortcuts_explicit;
  gboolean remove;
  gboolean dry_run;
} SetupOptions;

static void setup_status(const char *status, const char *subject,
                         const char *detail);

static gboolean setup_run_command(char **argv) {
  int exit_status = 0;
  g_autoptr(GError) error = NULL;
  return g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
                      NULL, &exit_status, &error) &&
         exit_status == 0;
}

/*
 * Applies file-backed Niri setup changes to the live compositor when IPC is
 * available. Reload failures are advisory: setup has already written the
 * managed config block, so the deterministic recovery path is a manual Niri
 * reload rather than an ERR_* setup failure.
 */
static void setup_niri_reload_config(const char *path) {
  if (path == NULL) {
    setup_status("reload needed", "Niri config",
                 "run `niri validate && niri msg action load-config-file`");
    return;
  }

  char *validate_argv[] = {"niri", "validate", "-c", (char *)path, NULL};
  char *reload_argv[] = {"niri", "msg", "action", "load-config-file", NULL};
  if (setup_run_command(validate_argv) && setup_run_command(reload_argv))
    setup_status("reloaded", "Niri config", path);
  else
    setup_status("reload needed", "Niri config",
                 "run `niri validate && niri msg action load-config-file`");
}

static gboolean setup_can_prompt(void) {
  int fd = open("/dev/tty", O_RDWR);
  if (fd < 0)
    return FALSE;
  (void)close(fd);
  return TRUE;
}

static gboolean setup_prompt(const char *question, gboolean default_yes) {
  FILE *terminal = fopen("/dev/tty", "r+");
  if (terminal == NULL)
    return FALSE;
  (void)fprintf(terminal, "%s %s ", question, default_yes ? "[Y/n]" : "[y/N]");
  (void)fflush(terminal);
  char answer[32] = {0};
  gboolean accepted = FALSE;
  if (fgets(answer, sizeof(answer), terminal) != NULL) {
    accepted = g_ascii_strncasecmp(answer, "y", 1) == 0 ||
               (default_yes && (answer[0] == '\n' || answer[0] == '\0'));
  }
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
          "Usage: shaula setup [--yes] [--shortcuts|--no-shortcuts] "
          "[--niri] [--noctalia] [--niri-keybinds] [--no-integrations] "
          "[--no-niri] [--no-noctalia] [--remove] [--dry-run]\n");
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
    else if (g_str_equal(argv[i], "--shortcuts")) {
      if (options->shortcuts_explicit && !options->shortcuts)
        return FALSE;
      options->shortcuts = TRUE;
      options->shortcuts_explicit = TRUE;
    } else if (g_str_equal(argv[i], "--no-shortcuts")) {
      if (options->shortcuts_explicit && options->shortcuts)
        return FALSE;
      options->shortcuts = FALSE;
      options->shortcuts_explicit = TRUE;
    } else if (g_str_equal(argv[i], "--niri-keybinds")) {
      if (options->shortcuts_explicit && !options->shortcuts)
        return FALSE;
      options->shortcuts = TRUE;
      options->shortcuts_explicit = TRUE;
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
                                   const char *path,
                                   gboolean *niri_reload_needed) {
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
  if (result.changed && !options->dry_run && niri_reload_needed != NULL)
    *niri_reload_needed = TRUE;
  managed_block_result_clear(&result);

  return TRUE;
}

static gboolean setup_niri_remove(const SetupOptions *options,
                                  const char *path,
                                  gboolean *niri_reload_needed) {
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
  if (result.changed && !options->dry_run && niri_reload_needed != NULL)
    *niri_reload_needed = TRUE;
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
  if (result.changed && !options->dry_run && niri_reload_needed != NULL)
    *niri_reload_needed = TRUE;
  managed_block_result_clear(&result);
  return TRUE;
}

static gboolean setup_shortcuts(const SetupOptions *options,
                                gboolean interactive,
                                gboolean *niri_reload_needed) {
  ShaulaShortcutOptions shortcut_options = {
      .dry_run = options->dry_run,
      .remember_choice = !options->remove,
  };
  ShaulaShortcutStatus status;
  shaula_shortcut_status_init(&status);
  g_autofree char *error = NULL;

  if (options->remove) {
    ShaulaShortcutResult before =
        shaula_shortcuts_query(&status, &error);
    const gboolean was_niri_active =
        before == SHAULA_SHORTCUT_RESULT_OK &&
        status.backend == SHAULA_SHORTCUT_BACKEND_NIRI &&
        status.state == SHAULA_SHORTCUT_STATE_ACTIVE;
    g_clear_pointer(&error, g_free);
    ShaulaShortcutResult result =
        shaula_shortcuts_disable(&shortcut_options, &status, &error);
    if (result == SHAULA_SHORTCUT_RESULT_OK && !options->dry_run &&
        !shaula_shortcuts_reset_setup_state(&error))
      result = SHAULA_SHORTCUT_RESULT_CONFIG_INVALID;
    setup_status(result == SHAULA_SHORTCUT_RESULT_OK ? "removed" : "failed",
                 "capture shortcuts",
                 error != NULL ? error : shaula_shortcut_state_token(status.state));
    if (result == SHAULA_SHORTCUT_RESULT_OK && was_niri_active &&
        !options->dry_run && niri_reload_needed != NULL)
      *niri_reload_needed = TRUE;
    shaula_shortcut_status_clear(&status);
    return result == SHAULA_SHORTCUT_RESULT_OK;
  }

  ShaulaShortcutResult queried = shaula_shortcuts_query(&status, &error);
  if (queried != SHAULA_SHORTCUT_RESULT_OK) {
    setup_status("failed", "capture shortcuts",
                 error != NULL ? error : "status unavailable");
    shaula_shortcut_status_clear(&status);
    return FALSE;
  }
  const gboolean was_niri_active =
      status.backend == SHAULA_SHORTCUT_BACKEND_NIRI &&
      status.state == SHAULA_SHORTCUT_STATE_ACTIVE;

  gboolean enable = options->shortcuts;
  gboolean make_choice = options->shortcuts_explicit;
  if (!make_choice && status.choice == SHAULA_SHORTCUT_CHOICE_ENABLED &&
      status.backend == SHAULA_SHORTCUT_BACKEND_NONE &&
      status.state == SHAULA_SHORTCUT_STATE_UNSUPPORTED) {
    enable = TRUE;
    make_choice = TRUE;
  }
  if (!make_choice && !status.setup_completed && options->assume_yes) {
    enable = TRUE;
    make_choice = TRUE;
  }
  if (!make_choice && !status.setup_completed && interactive &&
      !options->assume_yes) {
    enable = setup_prompt("Enable Ctrl+Shift+1–4 capture shortcuts?", TRUE);
    make_choice = TRUE;
  }
  if (!make_choice) {
    setup_status("skipped", "capture shortcuts",
                 status.setup_completed
                     ? shaula_shortcut_state_token(status.state)
                     : "no explicit choice in noninteractive setup");
    shaula_shortcut_status_clear(&status);
    return TRUE;
  }

  ShaulaShortcutResult result =
      enable ? shaula_shortcuts_enable(&shortcut_options, &status, &error)
             : shaula_shortcuts_disable(&shortcut_options, &status, &error);
  const char *state = shaula_shortcut_state_token(status.state);
  if (result != SHAULA_SHORTCUT_RESULT_OK) {
    setup_status("failed", "capture shortcuts",
                 error != NULL ? error : state);
    shaula_shortcut_status_clear(&status);
    return FALSE;
  }
  if (!enable)
    setup_status(options->dry_run ? "would-disable" : "disabled",
                 "capture shortcuts", "choice remembered");
  else if (status.state == SHAULA_SHORTCUT_STATE_UNSUPPORTED)
    setup_status("skipped", "capture shortcuts",
                 "portal and Niri shortcuts unavailable; Shaula menu remains available");
  else if (status.state == SHAULA_SHORTCUT_STATE_PERMISSION_DENIED)
    setup_status("skipped", "capture shortcuts", "desktop permission denied");
  else if (status.state == SHAULA_SHORTCUT_STATE_PERMISSION_PENDING)
    setup_status("pending", "capture shortcuts", "waiting for desktop approval");
  else
    setup_status(options->dry_run ? "would-enable" : "enabled",
                 "capture shortcuts", shaula_shortcut_backend_token(status.backend));
  if (enable && !was_niri_active &&
      status.backend == SHAULA_SHORTCUT_BACKEND_NIRI &&
      status.state == SHAULA_SHORTCUT_STATE_ACTIVE && !options->dry_run &&
      niri_reload_needed != NULL)
    *niri_reload_needed = TRUE;
  shaula_shortcut_status_clear(&status);
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

  const gboolean interactive = setup_can_prompt();
  gboolean niri_reload_needed = FALSE;
  gboolean ok = setup_shortcuts(&options, interactive, &niri_reload_needed);
  if (!options.integrations) {
    setup_status("skipped", "optional integrations", "disabled");
    if (niri_reload_needed) {
      g_autofree char *reload_path = niri_config_path(NULL);
      setup_niri_reload_config(reload_path);
    }
    setup_status(ok ? "installed" : "failed", "setup",
                 ok ? "core configuration ready" : "shortcut cleanup incomplete");
    return ok ? 0 : 1;
  }

  g_autofree char *niri_path = niri_config_path(NULL);
  const gboolean niri_detected =
      options.niri && niri_path != NULL &&
      g_file_test(niri_path, G_FILE_TEST_IS_REGULAR);
  gboolean use_niri = options.niri_explicit || options.assume_yes;
  if (niri_detected && !options.remove && !use_niri && interactive)
    use_niri = setup_prompt("Install Shaula's Niri window rule?", FALSE);
  if (options.remove)
    use_niri = niri_detected;
  if (!niri_detected) {
    setup_status("skipped", "Niri integration", "not detected");
  } else if (!use_niri) {
    setup_status("skipped", "Niri integration",
                 interactive ? "declined" : "noninteractive");
  } else {
    ok = (options.remove
              ? setup_niri_remove(&options, niri_path, &niri_reload_needed)
              : setup_niri_install(&config, &options, niri_path,
                                   &niri_reload_needed)) &&
         ok;
  }

  const gboolean noctalia_detected =
      options.noctalia && shaula_noctalia_detected();
  gboolean use_noctalia = options.noctalia_explicit || options.assume_yes;
  if (noctalia_detected && !options.remove && !use_noctalia && interactive)
    use_noctalia =
        setup_prompt("Install the Shaula Noctalia widget?", FALSE);
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

  if (niri_reload_needed)
    setup_niri_reload_config(niri_path);

  setup_status(ok ? "installed" : "failed", "setup",
               ok ? "complete" : "optional integration incomplete");
  return ok ? 0 : 1;
}
