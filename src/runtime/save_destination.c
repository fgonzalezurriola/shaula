#include "save_destination.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

static gboolean template_token_at(const char *cursor, gsize *length) {
  static const char *const tokens[] = {"{date}", "{time}", "{datetime}"};
  for (gsize i = 0; i < G_N_ELEMENTS(tokens); i++) {
    if (g_str_has_prefix(cursor, tokens[i])) {
      *length = strlen(tokens[i]);
      return TRUE;
    }
  }
  return FALSE;
}

gboolean shaula_save_folder_validate(const char *folder) {
  if (folder == NULL || folder[0] == '\0' || !g_utf8_validate(folder, -1, NULL))
    return FALSE;
  if (!g_path_is_absolute(folder) && !g_str_equal(folder, "~") &&
      !g_str_has_prefix(folder, "~/"))
    return FALSE;
  for (const char *cursor = folder; *cursor != '\0'; cursor++) {
    if ((guchar)*cursor < 0x20 || *cursor == '\\' || *cursor == '"')
      return FALSE;
  }
  return TRUE;
}

gboolean shaula_save_template_validate(const char *filename_template) {
  if (filename_template == NULL || filename_template[0] == '\0' ||
      strlen(filename_template) > 120 ||
      !g_utf8_validate(filename_template, -1, NULL))
    return FALSE;

  gboolean has_output = FALSE;
  for (const char *cursor = filename_template; *cursor != '\0';) {
    gsize token_length = 0;
    if (*cursor == '{') {
      if (!template_token_at(cursor, &token_length))
        return FALSE;
      cursor += token_length;
      has_output = TRUE;
      continue;
    }
    if (*cursor == '}' || *cursor == '/' || *cursor == '\\' ||
        *cursor == ':' || *cursor == '"' || (guchar)*cursor < 0x20)
      return FALSE;
    has_output = TRUE;
    cursor = g_utf8_next_char(cursor);
  }

  if (!has_output || filename_template[0] == '.' ||
      g_str_has_suffix(filename_template, ".") ||
      g_str_has_suffix(filename_template, " ") ||
      g_ascii_strcasecmp(filename_template, ".png") == 0 ||
      g_str_has_suffix(filename_template, ".png"))
    return FALSE;
  return TRUE;
}

static char *expanded_folder(const char *folder) {
  const char *configured = folder != NULL && folder[0] != '\0'
                               ? folder
                               : SHAULA_SAVE_DEFAULT_FOLDER;
  if (!shaula_save_folder_validate(configured))
    return NULL;
  if (g_path_is_absolute(configured))
    return g_canonicalize_filename(configured, NULL);

  const char *home = g_get_home_dir();
  if (home == NULL || home[0] == '\0')
    return NULL;
  if (g_str_equal(configured, "~"))
    return g_strdup(home);
  return g_canonicalize_filename(configured + 2, home);
}

char *shaula_save_directory_resolve(const char *folder, gboolean create,
                                    GError **error) {
  g_autofree char *directory = expanded_folder(folder);
  if (directory == NULL) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                "The screenshot folder must be an absolute path or start with ~/.");
    return NULL;
  }
  if (create && g_mkdir_with_parents(directory, 0755) != 0) {
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                "Could not create screenshot folder %s: %s", directory,
                g_strerror(errno));
    return NULL;
  }
  if (!g_file_test(directory, G_FILE_TEST_IS_DIR) ||
      g_access(directory, W_OK | X_OK) != 0) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_ACCES,
                "Screenshot folder is not writable: %s", directory);
    return NULL;
  }
  return g_steal_pointer(&directory);
}

static void append_formatted(GString *output, GDateTime *now,
                             const char *format) {
  g_autofree char *formatted = g_date_time_format(now, format);
  if (formatted != NULL)
    g_string_append(output, formatted);
}

char *shaula_save_basename_new(const char *filename_template, GDateTime *now) {
  const char *configured = filename_template != NULL && filename_template[0] != '\0'
                               ? filename_template
                               : SHAULA_SAVE_DEFAULT_TEMPLATE;
  if (!shaula_save_template_validate(configured))
    return NULL;

  g_autoptr(GDateTime) owned_now =
      now == NULL ? g_date_time_new_now_local() : NULL;
  GDateTime *timestamp = now != NULL ? now : owned_now;
  if (timestamp == NULL)
    return NULL;

  GString *stem = g_string_new(NULL);
  for (const char *cursor = configured; *cursor != '\0';) {
    if (g_str_has_prefix(cursor, "{datetime}")) {
      append_formatted(stem, timestamp, "%Y%m%d-%H%M%S");
      cursor += strlen("{datetime}");
    } else if (g_str_has_prefix(cursor, "{date}")) {
      append_formatted(stem, timestamp, "%Y%m%d");
      cursor += strlen("{date}");
    } else if (g_str_has_prefix(cursor, "{time}")) {
      append_formatted(stem, timestamp, "%H%M%S");
      cursor += strlen("{time}");
    } else {
      gunichar character = g_utf8_get_char(cursor);
      g_string_append_unichar(stem, character);
      cursor = g_utf8_next_char(cursor);
    }
  }
  g_string_append(stem, ".png");
  return g_string_free(stem, FALSE);
}

char *shaula_save_unique_path_new(const char *directory,
                                  const char *filename_template,
                                  GDateTime *now, GError **error) {
  if (directory == NULL || directory[0] == '\0') {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                "Screenshot folder is unavailable.");
    return NULL;
  }
  g_autofree char *basename =
      shaula_save_basename_new(filename_template, now);
  if (basename == NULL) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                "Filename template is invalid.");
    return NULL;
  }

  gsize suffix_length = strlen(".png");
  g_autofree char *stem =
      g_strndup(basename, strlen(basename) - suffix_length);
  for (guint attempt = 0; attempt < 1000; attempt++) {
    g_autofree char *name = attempt == 0
                                ? g_strdup(basename)
                                : g_strdup_printf("%s-%u.png", stem, attempt + 1);
    char *path = g_build_filename(directory, name, NULL);
    if (!g_file_test(path, G_FILE_TEST_EXISTS))
      return path;
    g_free(path);
  }
  g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_EXIST,
              "Could not allocate a unique screenshot filename.");
  return NULL;
}
