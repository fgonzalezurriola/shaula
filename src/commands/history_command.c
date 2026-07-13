#include "commands.h"

#include "support.h"

#include <glib.h>

typedef struct {
  char *path;
  char *mime;
  guint width;
  guint height;
  char *backend;
  char *timestamp;
} HistoryEntry;

static void history_entry_free(gpointer data) {
  HistoryEntry *entry = data;
  if (entry == NULL)
    return;
  g_free(entry->path);
  g_free(entry->mime);
  g_free(entry->backend);
  g_free(entry->timestamp);
  g_free(entry);
}

static GPtrArray *history_entries_load(void) {
  GPtrArray *entries = g_ptr_array_new_with_free_func(history_entry_free);
  g_autofree char *contents = NULL;
  if (!g_file_get_contents("/tmp/shaula/history/latest.v1", &contents, NULL,
                           NULL))
    return entries;

  g_auto(GStrv) lines = g_strsplit(contents, "\n", -1);
  for (gsize i = 0; lines[i] != NULL && entries->len < 20; i++) {
    if (lines[i][0] == '\0')
      continue;
    g_auto(GStrv) fields = g_strsplit(lines[i], "|", 6);
    if (g_strv_length(fields) != 6)
      continue;
    char *width_end = NULL;
    char *height_end = NULL;
    guint64 width = g_ascii_strtoull(fields[2], &width_end, 10);
    guint64 height = g_ascii_strtoull(fields[3], &height_end, 10);
    if (width_end == fields[2] || *width_end != '\0' ||
        height_end == fields[3] || *height_end != '\0' || width > G_MAXUINT ||
        height > G_MAXUINT)
      continue;
    HistoryEntry *entry = g_new0(HistoryEntry, 1);
    entry->path = g_strdup(fields[0]);
    entry->mime = g_strdup(fields[1]);
    entry->width = (guint)width;
    entry->height = (guint)height;
    entry->backend = g_strdup(fields[4]);
    entry->timestamp = g_strdup(fields[5]);
    g_ptr_array_add(entries, entry);
  }
  return entries;
}

static void history_entry_append_json(GString *output,
                                      const HistoryEntry *entry) {
  g_autofree char *path = shaula_command_json_string(entry->path);
  g_autofree char *mime = shaula_command_json_string(entry->mime);
  g_autofree char *backend = shaula_command_json_string(entry->backend);
  g_autofree char *timestamp = shaula_command_json_string(entry->timestamp);
  g_string_append_printf(
      output,
      "{\"path\":%s,\"mime\":%s,\"dimensions\":{\"width\":%u,"
      "\"height\":%u},\"backend_used\":%s,\"timestamp\":%s}",
      path, mime, entry->width, entry->height, backend, timestamp);
}

int shaula_history_command_run(int argc, char **argv) {
  if (argc < 4)
    return shaula_command_write_error(
        "history", "ERR_CLI_USAGE",
        "usage: shaula history <list|show> --json", "{}");

  const char *subcommand = argv[2];
  if (!g_str_equal(subcommand, "list") && !g_str_equal(subcommand, "show"))
    return shaula_command_write_error("history", "ERR_CLI_USAGE",
                                      "unsupported history subcommand", "{}");

  gboolean json = FALSE;
  const char *id = NULL;
  for (int i = 3; i < argc; i++) {
    if (g_str_equal(argv[i], "--json")) {
      json = TRUE;
      continue;
    }
    if (g_str_equal(subcommand, "show") && g_str_equal(argv[i], "--id")) {
      if (i + 1 >= argc)
        return shaula_command_write_error(
            "history show", "ERR_CLI_USAGE", "--id requires an entry id",
            "{}");
      id = argv[++i];
      continue;
    }
    return shaula_command_write_error(
        g_str_equal(subcommand, "show") ? "history show" : "history list",
        "ERR_CLI_USAGE", "unsupported flag", "{}");
  }
  if (!json)
    return shaula_command_write_error(
        g_str_equal(subcommand, "show") ? "history show" : "history list",
        "ERR_CLI_USAGE", "--json is required", "{}");

  g_autoptr(GPtrArray) entries = history_entries_load();
  if (g_str_equal(subcommand, "show")) {
    if (id == NULL)
      return shaula_command_write_error("history show", "ERR_CLI_USAGE",
                                        "--id is required", "{}");
    if (!g_str_equal(id, "latest") || entries->len == 0)
      return shaula_command_write_error(
          "history show", "ERR_HISTORY_ENTRY_NOT_FOUND",
          "history entry was not found", "{}");
    GString *result = g_string_new("{\"id\":\"latest\",\"entry\":");
    history_entry_append_json(result, g_ptr_array_index(entries, 0));
    g_string_append_c(result, '}');
    int status =
        shaula_command_write_success("history show", result->str, "[]");
    g_string_free(result, TRUE);
    return status;
  }

  GString *result = g_string_new("{\"entries\":[");
  for (guint i = 0; i < entries->len; i++) {
    if (i > 0)
      g_string_append_c(result, ',');
    history_entry_append_json(result, g_ptr_array_index(entries, i));
  }
  g_string_append(result, "]}");
  int status =
      shaula_command_write_success("history list", result->str, "[]");
  g_string_free(result, TRUE);
  return status;
}
