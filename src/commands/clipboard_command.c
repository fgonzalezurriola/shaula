#include "commands.h"

#include "runtime/process_exec.h"
#include "support.h"

#include <glib.h>
#include <time.h>

static gboolean clipboard_available(void) {
  const char *value = g_getenv("SHAULA_CLIPBOARD_AVAILABLE");
  if (value == NULL)
    return TRUE;
  return g_ascii_strcasecmp(value, "0") != 0 &&
         g_ascii_strcasecmp(value, "false") != 0;
}

int shaula_clipboard_command_run(int argc, char **argv) {
  static const char *clipboard_dir = "/tmp/shaula/clipboard";
  static const char *clipboard_state =
      "/tmp/shaula/clipboard/current-image.path";

  if (argc < 4)
    return shaula_command_write_error(
        "clipboard", "ERR_CLI_USAGE",
        "usage: shaula clipboard <copy-image|import-image> --json", "{}");

  const char *subcommand = argv[2];
  gboolean json = FALSE;
  const char *input = NULL;
  const char *output = NULL;
  for (int i = 3; i < argc; i++) {
    if (g_str_equal(argv[i], "--json"))
      json = TRUE;
    else if (g_str_equal(argv[i], "--input") && i + 1 < argc)
      input = argv[++i];
    else if (g_str_equal(argv[i], "--output") && i + 1 < argc)
      output = argv[++i];
    else
      return shaula_command_write_error("clipboard", "ERR_CLI_USAGE",
                                        "unsupported flag", "{}");
  }
  if (!json)
    return shaula_command_write_error("clipboard", "ERR_CLI_USAGE",
                                      "--json is required", "{}");
  if (!g_str_equal(subcommand, "copy-image") &&
      !g_str_equal(subcommand, "import-image"))
    return shaula_command_write_error(
        "clipboard", "ERR_CLI_USAGE", "unsupported clipboard subcommand",
        "{}");
  if (!clipboard_available())
    return shaula_command_write_error(
        g_str_equal(subcommand, "copy-image") ? "clipboard copy-image"
                                               : "clipboard import-image",
        "ERR_CLIPBOARD_UNAVAILABLE", "clipboard backend is unavailable",
        "{}");

  if (g_str_equal(subcommand, "copy-image")) {
    if (input == NULL)
      return shaula_command_write_error("clipboard copy-image",
                                        "ERR_CLI_USAGE",
                                        "--input is required", "{}");
    g_autofree char *bytes = NULL;
    gsize length = 0;
    if (!g_file_get_contents(input, &bytes, &length, NULL))
      return shaula_command_write_error(
          "clipboard copy-image", "ERR_CLIPBOARD_COPY_FAILED",
          "clipboard image copy failed", "{}");
    if (g_mkdir_with_parents(clipboard_dir, 0755) != 0)
      return shaula_command_write_error(
          "clipboard copy-image", "ERR_CLIPBOARD_COPY_FAILED",
          "clipboard image copy failed", "{}");

    g_autofree char *state_contents = g_strconcat(input, "\n", NULL);
    if (!g_file_set_contents(clipboard_state, state_contents, -1, NULL))
      return shaula_command_write_error(
          "clipboard copy-image", "ERR_CLIPBOARD_COPY_FAILED",
          "clipboard image copy failed", "{}");

    const char *arguments[] = {"wl-copy", "--type", "image/png", NULL};
    ShaulaProcessTermKind term = SHAULA_PROCESS_TERM_UNKNOWN;
    guint32 value = 0;
    if (shaula_process_run_with_input(arguments, bytes, length, &term, &value) !=
            SHAULA_PROCESS_STATUS_OK ||
        term != SHAULA_PROCESS_TERM_EXITED || value != 0)
      return shaula_command_write_error(
          "clipboard copy-image", "ERR_CLIPBOARD_COPY_FAILED",
          "clipboard image copy failed", "{}");

    g_autofree char *input_json = shaula_command_json_string(input);
    g_autofree char *result = g_strdup_printf(
        "{\"input\":%s,\"copied\":true}", input_json);
    return shaula_command_write_success("clipboard copy-image", result, "[]");
  }

  g_autofree char *state_contents = NULL;
  if (!g_file_get_contents(clipboard_state, &state_contents, NULL, NULL))
    return shaula_command_write_error(
        "clipboard import-image", "ERR_CLIPBOARD_IMPORT_INVALID",
        "clipboard image import failed", "{}");
  g_strstrip(state_contents);
  if (state_contents[0] == '\0')
    return shaula_command_write_error(
        "clipboard import-image", "ERR_CLIPBOARD_IMPORT_INVALID",
        "clipboard image import failed", "{}");

  g_autofree char *source = g_strdup(state_contents);
  g_autofree char *resolved =
      output != NULL
          ? g_strdup(output)
          : g_strdup_printf("/tmp/shaula/imported-%" G_GINT64_FORMAT ".png",
                            (gint64)time(NULL));
  g_autofree char *bytes = NULL;
  gsize length = 0;
  g_autofree char *parent = g_path_get_dirname(resolved);
  if (!g_file_get_contents(source, &bytes, &length, NULL) ||
      g_mkdir_with_parents(parent, 0755) != 0 ||
      !g_file_set_contents(resolved, bytes, (gssize)length, NULL))
    return shaula_command_write_error(
        "clipboard import-image", "ERR_CLIPBOARD_IMPORT_INVALID",
        "clipboard image import failed", "{}");

  g_autofree char *path_json = shaula_command_json_string(resolved);
  g_autofree char *result = g_strdup_printf("{\"path\":%s}", path_json);
  return shaula_command_write_success("clipboard import-image", result, "[]");
}
