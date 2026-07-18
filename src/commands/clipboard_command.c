#include "commands.h"

#include "clipboard/clipboard.h"
#include "support.h"

#include <glib.h>
#include <string.h>
#include <time.h>

static int clipboard_publish_error(const char *command, const char *message,
                                   ShaulaClipboardStatus status) {
  g_autofree char *details = g_strdup_printf(
      "{\"provider_status\":\"%s\"}",
      shaula_clipboard_status_token(status));
  if (status == SHAULA_CLIPBOARD_STATUS_UNAVAILABLE ||
      status == SHAULA_CLIPBOARD_STATUS_SPAWN_FAILED)
    return shaula_command_write_error(
        command, "ERR_CLIPBOARD_UNAVAILABLE",
        "clipboard backend is unavailable", details);
  return shaula_command_write_error(command, "ERR_CLIPBOARD_COPY_FAILED",
                                    message, details);
}

int shaula_clipboard_command_run(int argc, char **argv) {
  static const char *clipboard_dir = "/tmp/shaula/clipboard";
  static const char *clipboard_state =
      "/tmp/shaula/clipboard/current-image.path";

  if (argc < 4)
    return shaula_command_write_error(
        "clipboard", "ERR_CLI_USAGE",
        "usage: shaula clipboard <copy-image|copy-text|import-image> --json",
        "{}");

  const char *subcommand = argv[2];
  gboolean json = FALSE;
  const char *input = NULL;
  const char *output = NULL;
  const char *text = NULL;
  for (int i = 3; i < argc; i++) {
    if (g_str_equal(argv[i], "--json"))
      json = TRUE;
    else if (g_str_equal(argv[i], "--input") && i + 1 < argc)
      input = argv[++i];
    else if (g_str_equal(argv[i], "--output") && i + 1 < argc)
      output = argv[++i];
    else if (g_str_equal(argv[i], "--text") && i + 1 < argc)
      text = argv[++i];
    else
      return shaula_command_write_error("clipboard", "ERR_CLI_USAGE",
                                        "unsupported flag", "{}");
  }
  if (!json)
    return shaula_command_write_error("clipboard", "ERR_CLI_USAGE",
                                      "--json is required", "{}");
  if (!g_str_equal(subcommand, "copy-image") &&
      !g_str_equal(subcommand, "copy-text") &&
      !g_str_equal(subcommand, "import-image"))
    return shaula_command_write_error(
        "clipboard", "ERR_CLI_USAGE", "unsupported clipboard subcommand",
        "{}");

  if (g_str_equal(subcommand, "copy-image")) {
    if (input == NULL)
      return shaula_command_write_error("clipboard copy-image",
                                        "ERR_CLI_USAGE",
                                        "--input is required", "{}");
    ShaulaClipboardStatus status = shaula_clipboard_publish_png_file(input);
    if (status != SHAULA_CLIPBOARD_STATUS_OK)
      return clipboard_publish_error("clipboard copy-image",
                                     "clipboard image copy failed", status);

    if (g_mkdir_with_parents(clipboard_dir, 0755) != 0)
      return shaula_command_write_error(
          "clipboard copy-image", "ERR_CLIPBOARD_COPY_FAILED",
          "clipboard image copy failed", "{\"state\":\"unwritable\"}");
    g_autofree char *state_contents = g_strconcat(input, "\n", NULL);
    if (!g_file_set_contents(clipboard_state, state_contents, -1, NULL))
      return shaula_command_write_error(
          "clipboard copy-image", "ERR_CLIPBOARD_COPY_FAILED",
          "clipboard image copy failed", "{\"state\":\"unwritable\"}");

    g_autofree char *input_json = shaula_command_json_string(input);
    g_autofree char *result = g_strdup_printf(
        "{\"input\":%s,\"copied\":true,\"provider\":\"shaula\"}",
        input_json);
    return shaula_command_write_success("clipboard copy-image", result, "[]");
  }

  if (g_str_equal(subcommand, "copy-text")) {
    if ((text == NULL) == (input == NULL))
      return shaula_command_write_error(
          "clipboard copy-text", "ERR_CLI_USAGE",
          "exactly one of --text or --input is required", "{}");
    g_autofree char *file_text = NULL;
    gsize length = 0U;
    if (input != NULL) {
      if (!g_file_get_contents(input, &file_text, &length, NULL) ||
          length == 0U)
        return shaula_command_write_error(
            "clipboard copy-text", "ERR_CLIPBOARD_COPY_FAILED",
            "clipboard text copy failed", "{\"input\":\"unreadable\"}");
      text = file_text;
    } else {
      length = strlen(text);
    }
    ShaulaClipboardStatus status =
        shaula_clipboard_publish_text(text, length);
    if (status != SHAULA_CLIPBOARD_STATUS_OK)
      return clipboard_publish_error("clipboard copy-text",
                                     "clipboard text copy failed", status);
    g_autofree char *result = g_strdup_printf(
        "{\"copied\":true,\"bytes\":%zu,\"provider\":\"shaula\"}",
        (size_t)length);
    return shaula_command_write_success("clipboard copy-text", result, "[]");
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
