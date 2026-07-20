#include "commands.h"

#include "config/config.h"
#include "config/niri_managed.h"
#include "config_command_support.h"
#include "settings/settings_config.h"
#include "support.h"

#include <glib.h>
#include <string.h>

static int write_managed_result(const char *command,
                                const ManagedBlockResult *result,
                                gboolean dry_run) {
  g_autofree char *path = shaula_command_json_string(result->path);
  g_autofree char *backup =
      result->backup_path != NULL
          ? shaula_command_json_string(result->backup_path)
          : g_strdup("null");
  g_autofree char *body = g_strdup_printf(
      "{\"path\":%s,\"backup_path\":%s,\"installed\":%s,"
      "\"replaced\":%s,\"changed\":%s,\"dry_run\":%s}",
      path, backup, shaula_command_json_bool(result->installed),
      shaula_command_json_bool(result->replaced),
      shaula_command_json_bool(result->changed),
      shaula_command_json_bool(dry_run));
  return shaula_command_write_success(command, body, "[]");
}

int shaula_config_command_run(int argc, char **argv) {
  if (argc < 4)
    return shaula_command_write_error(
        "config", "ERR_CLI_USAGE",
        "usage: shaula config <show|init|save|niri-window-rule|niri-install> "
        "--json",
        "{}");

  const char *subcommand = argv[2];
  g_autofree char *command = g_strdup_printf("config %s", subcommand);
  gboolean json_mode = FALSE;
  gboolean dry_run = FALSE;
  gboolean force = FALSE;
  gboolean apply_niri = FALSE;
  const char *path_override = NULL;
  g_autofree char *path = shaula_config_path_new();
  ShaulaConfig config;
  gboolean loaded = FALSE;
  ShaulaConfigStatus load_status = shaula_config_load(path, &config, &loaded);
  if (load_status == SHAULA_CONFIG_STATUS_INVALID)
    return shaula_command_write_error(command, "ERR_CONFIG_INVALID",
                                      "invalid configuration file", "{}");
  if (load_status == SHAULA_CONFIG_STATUS_UNREADABLE)
    return shaula_command_write_error(command, "ERR_CONFIG_UNREADABLE",
                                      "configuration file is unreadable",
                                      "{}");

  gboolean setting_seen = FALSE;
  for (int i = 3; i < argc; i++) {
    if (g_str_equal(argv[i], "--json")) {
      json_mode = TRUE;
      continue;
    }
    if (g_str_equal(argv[i], "--dry-run")) {
      dry_run = TRUE;
      continue;
    }
    if (g_str_equal(argv[i], "--force")) {
      force = TRUE;
      continue;
    }
    if (g_str_equal(argv[i], "--apply-niri")) {
      apply_niri = TRUE;
      continue;
    }
    if (g_str_equal(argv[i], "--path") && i + 1 < argc) {
      path_override = argv[++i];
      continue;
    }
    if (i + 1 < argc && shaula_settings_config_apply_cli_flag(
                            &config, argv[i], argv[i + 1])) {
      setting_seen = TRUE;
      i++;
      continue;
    }
    return shaula_command_write_error(command, "ERR_CLI_USAGE",
                                      "unsupported flag", "{}");
  }
  if (!json_mode)
    return shaula_command_write_error(command, "ERR_CLI_USAGE",
                                      "--json is required", "{}");

  if (g_str_equal(subcommand, "show")) {
    g_autofree char *path_json =
        shaula_command_json_string(path != NULL ? path : "");
    g_autofree char *body = shaula_settings_config_public_json_new(&config);
    g_autofree char *result = g_strdup_printf(
        "{\"path\":%s,\"loaded\":%s,\"config\":%s}", path_json,
        shaula_command_json_bool(loaded), body);
    return shaula_command_write_success(command, result, "[]");
  }

  if (g_str_equal(subcommand, "init")) {
    if (path == NULL)
      return shaula_command_write_error(
          command, "ERR_CONFIG_UNREADABLE",
          "configuration path could not be resolved", "{}");
    gboolean changed = force || !g_file_test(path, G_FILE_TEST_EXISTS);
    if (changed && !dry_run &&
        shaula_config_save(path, &config) != SHAULA_CONFIG_STATUS_OK)
      return shaula_command_write_error(
          command, "ERR_CONFIG_UNREADABLE",
          "configuration file could not be created", "{}");
    g_autofree char *path_json = shaula_command_json_string(path);
    g_autofree char *result = g_strdup_printf(
        "{\"path\":%s,\"created\":%s,\"changed\":%s,\"dry_run\":%s}",
        path_json, shaula_command_json_bool(changed),
        shaula_command_json_bool(changed), shaula_command_json_bool(dry_run));
    return shaula_command_write_success(command, result, "[]");
  }

  if (g_str_equal(subcommand, "save")) {
    if (!setting_seen)
      return shaula_command_write_error(
          command, "ERR_CLI_USAGE",
          "config save requires at least one setting flag", "{}");
    if (!shaula_config_validate(&config))
      return shaula_command_write_error(
          command, "ERR_CLI_USAGE", "skip preview requires copy or save",
          "{\"field\":\"capture.after\"}");
    if (!dry_run &&
        shaula_config_save(path, &config) != SHAULA_CONFIG_STATUS_OK)
      return shaula_command_write_error(command, "ERR_CONFIG_UNREADABLE",
                                        "configuration file is unreadable",
                                        "{}");

    ManagedBlockResult niri_result = {0};
    gboolean niri_invalid = FALSE;
    if (apply_niri) {
      g_autofree char *rule = shaula_config_command_preview_rule(&config);
      if (!install_managed_block(
              path_override, "// BEGIN SHAULA PREVIEW WINDOW RULE",
              "// END SHAULA PREVIEW WINDOW RULE", rule, dry_run,
              &niri_result, &niri_invalid)) {
        managed_block_result_clear(&niri_result);
        return shaula_command_write_error(
            command,
            niri_invalid ? "ERR_CONFIG_INVALID" : "ERR_CONFIG_UNREADABLE",
            niri_invalid
                ? "invalid Niri configuration managed block"
                : "Niri configuration path could not be resolved",
            "{}");
      }
    }
    g_autofree char *path_json =
        shaula_command_json_string(path != NULL ? path : "");
    g_autofree char *result = g_strdup_printf(
        "{\"path\":%s,\"saved\":true,\"changed\":true,\"dry_run\":%s,"
        "\"niri_applied\":%s}",
        path_json, shaula_command_json_bool(dry_run),
        shaula_command_json_bool(apply_niri));
    int status = shaula_command_write_success(command, result, "[]");
    managed_block_result_clear(&niri_result);
    return status;
  }

  if (g_str_equal(subcommand, "niri-window-rule")) {
    g_autofree char *rule = shaula_config_command_preview_rule(&config);
    g_autofree char *rule_json = shaula_command_json_string(rule);
    g_autofree char *path_json =
        shaula_command_json_string(path != NULL ? path : "");
    g_autofree char *result = g_strdup_printf(
        "{\"path\":%s,\"loaded\":%s,\"target\":\"preview\","
        "\"app_id\":\"dev.shaula.preview\",\"title\":\"Shaula Preview\","
        "\"kdl\":%s}",
        path_json, shaula_command_json_bool(loaded), rule_json);
    return shaula_command_write_success(command, result, "[]");
  }

  if (g_str_equal(subcommand, "niri-install")) {
    g_autofree char *payload = shaula_config_command_preview_rule(&config);
    ManagedBlockResult install_result = {0};
    gboolean invalid = FALSE;
    if (!install_managed_block(
            path_override, "// BEGIN SHAULA PREVIEW WINDOW RULE",
            "// END SHAULA PREVIEW WINDOW RULE", payload, dry_run,
            &install_result, &invalid)) {
      managed_block_result_clear(&install_result);
      return shaula_command_write_error(
          command, invalid ? "ERR_CONFIG_INVALID" : "ERR_CONFIG_UNREADABLE",
          invalid ? "invalid Niri configuration managed block"
                  : "Niri configuration path could not be resolved",
          "{}");
    }
    int status = write_managed_result(command, &install_result, dry_run);
    managed_block_result_clear(&install_result);
    return status;
  }

  return shaula_command_write_error("config", "ERR_CLI_USAGE",
                                    "unsupported config subcommand", "{}");
}
