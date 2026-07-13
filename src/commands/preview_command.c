#include "commands.h"

#include "config/config.h"
#include "preview/preview_result.h"
#include "runtime/helper_resolution.h"
#include "runtime/process_exec.h"
#include "support.h"

#include <glib.h>
#include <string.h>

int shaula_preview_command_run(int argc, char **argv) {
  if (argc != 4 || !g_str_equal(argv[3], "--json"))
    return shaula_command_write_error(
        "preview", "ERR_CLI_USAGE", "usage: shaula preview <file> --json",
        "{}");

  const char *path = argv[2];
  if (!g_file_test(path, G_FILE_TEST_IS_REGULAR))
    return shaula_command_write_error(
        "preview", "ERR_PREVIEW_INPUT_INVALID",
        "preview input image is not readable", "{}");

  g_autofree char *helper =
      shaula_executable_resolve_helper("SHAULA_PREVIEW_HELPER_BIN",
                                       "shaula-preview");
  if (helper == NULL)
    return shaula_command_write_error("preview", "ERR_PREVIEW_UNAVAILABLE",
                                      "preview helper is unavailable", "{}");

  g_autofree char *self = shaula_executable_current_path();
  g_auto(GStrv) envp = g_get_environ();
  envp = g_environ_setenv(envp, "SHAULA_BIN", self != NULL ? self : "shaula",
                          TRUE);
  envp = g_environ_setenv(envp, "SHAULA_PREVIEW_COPY_ON_ACCEPT", "0", TRUE);

  ShaulaConfig config;
  gboolean loaded = FALSE;
  g_autofree char *config_path = shaula_config_path_new();
  (void)shaula_config_load(config_path, &config, &loaded);
  envp = g_environ_setenv(envp, "SHAULA_PREVIEW_CLOSE_ON_SAVE",
                          config.close_preview_on_save ? "1" : "0", TRUE);
  g_autofree char *width = g_strdup_printf("%u", config.preview_width);
  g_autofree char *height = g_strdup_printf("%u", config.preview_height);
  envp = g_environ_setenv(envp, "SHAULA_PREVIEW_WINDOW_WIDTH", width, TRUE);
  envp = g_environ_setenv(envp, "SHAULA_PREVIEW_WINDOW_HEIGHT", height, TRUE);
  envp = g_environ_setenv(envp, "SHAULA_SAVE_FOLDER", config.save_folder, TRUE);

  const char *helper_argv[] = {helper, path, NULL};
  g_autofree char *stdout_text = NULL;
  g_autofree char *stderr_text = NULL;
  int exit_code = 0;
  if (shaula_process_run_sync((const char *const *)helper_argv,
                              (const char *const *)envp, &stdout_text,
                              &stderr_text, &exit_code) !=
          SHAULA_PROCESS_STATUS_OK ||
      exit_code != 0) {
    if (exit_code == 43)
      return shaula_command_write_error(
          "preview", "ERR_PREVIEW_INPUT_INVALID",
          "preview input image is invalid", "{}");
    return shaula_command_write_error("preview", "ERR_PREVIEW_UNAVAILABLE",
                                      "preview helper is unavailable", "{}");
  }

  ShaulaPreviewResult parsed;
  shaula_preview_result_init(&parsed);
  ShaulaPreviewResultStatus status = shaula_preview_result_parse(
      (ShaulaPreviewResultSpan){.data = (const guint8 *)stdout_text,
                                .length = stdout_text != NULL
                                              ? strlen(stdout_text)
                                              : 0},
      &parsed);
  if (status != SHAULA_PREVIEW_RESULT_STATUS_OK) {
    shaula_preview_result_clear(&parsed);
    return shaula_command_write_error(
        "preview", "ERR_PREVIEW_RESULT_INVALID",
        "preview helper did not emit valid result JSON", "{}");
  }

  ShaulaPreviewResultSpan action_span =
      shaula_preview_action_token(parsed.action);
  g_autofree char *action_text =
      g_strndup((const char *)action_span.data, action_span.length);
  g_autofree char *path_json = shaula_command_json_string(path);
  g_autofree char *action_json = shaula_command_json_string(action_text);
  g_autofree char *saved_path =
      parsed.saved_path.data != NULL
          ? g_strndup((const char *)parsed.saved_path.data,
                      parsed.saved_path.length)
          : NULL;
  g_autofree char *saved_path_json =
      saved_path != NULL ? shaula_command_json_string(saved_path)
                         : g_strdup("null");
  g_autofree char *result = g_strdup_printf(
      "{\"path\":%s,\"closed\":%s,\"action\":%s,\"copied\":%s,"
      "\"saved\":%s,\"saved_path\":%s}",
      path_json, shaula_command_json_bool(parsed.closed), action_json,
      shaula_command_json_bool(parsed.copied),
      shaula_command_json_bool(parsed.saved), saved_path_json);
  shaula_preview_result_clear(&parsed);
  return shaula_command_write_success("preview", result, "[]");
}
