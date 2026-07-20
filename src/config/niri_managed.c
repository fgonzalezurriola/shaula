#include "niri_managed.h"

#include <glib/gstdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void managed_block_result_clear(ManagedBlockResult *result) {
  g_clear_pointer(&result->path, g_free);
  g_clear_pointer(&result->backup_path, g_free);
}

char *niri_config_path(const char *override) {
  if (override != NULL && override[0] != '\0')
    return g_strdup(override);
  const char *configured = g_getenv("NIRI_CONFIG");
  if (configured != NULL && configured[0] != '\0')
    return g_strdup(configured);
  const char *xdg = g_getenv("XDG_CONFIG_HOME");
  if (xdg != NULL && xdg[0] != '\0')
    return g_build_filename(xdg, "niri", "config.kdl", NULL);
  const char *home = g_getenv("HOME");
  return home != NULL && home[0] != '\0'
             ? g_build_filename(home, ".config", "niri", "config.kdl", NULL)
             : NULL;
}

static guint count_text(const char *text, const char *needle) {
  guint count = 0;
  for (const char *cursor = text; (cursor = strstr(cursor, needle)) != NULL;
       cursor += strlen(needle))
    count++;
  return count;
}

/* Replaces only the named managed block and preserves all user-owned KDL. */
gboolean install_managed_block(const char *path_override,
                                      const char *begin_marker,
                                      const char *end_marker,
                                      const char *payload, gboolean dry_run,
                                      ManagedBlockResult *result,
                                      gboolean *invalid) {
  *result = (ManagedBlockResult){0};
  *invalid = FALSE;
  result->path = niri_config_path(path_override);
  if (result->path == NULL)
    return FALSE;
  g_autofree char *current = NULL;
  gsize current_length = 0;
  gboolean existed = g_file_test(result->path, G_FILE_TEST_EXISTS);
  if (existed && !g_file_get_contents(result->path, &current, &current_length,
                                      NULL))
    return FALSE;
  if (!existed) {
    current = g_strdup("");
    current_length = 0;
  }
  guint begins = count_text(current, begin_marker);
  guint ends = count_text(current, end_marker);
  const char *begin = strstr(current, begin_marker);
  const char *end = strstr(current, end_marker);
  if (begins > 1 || ends > 1 || begins != ends ||
      (begin != NULL && end < begin)) {
    *invalid = TRUE;
    return FALSE;
  }

  g_autofree char *block =
      g_strdup_printf("%s\n%s%s%s\n", begin_marker, payload,
                      g_str_has_suffix(payload, "\n") ? "" : "\n", end_marker);
  g_autofree char *replacement = NULL;
  if (begin != NULL) {
    const char *suffix = end + strlen(end_marker);
    if (*suffix == '\n')
      suffix++;
    replacement = g_strdup_printf("%.*s%s%s", (int)(begin - current), current,
                                  block, suffix);
    result->replaced = TRUE;
  } else {
    replacement = g_strdup_printf("%s%s%s", current,
                                  current_length > 0 && current[current_length - 1] != '\n'
                                      ? "\n"
                                      : "",
                                  block);
  }
  result->installed = TRUE;
  result->changed = !g_str_equal(current, replacement);
  if (!result->changed || dry_run)
    return TRUE;

  if (existed) {
    for (guint attempt = 0; attempt < 100; attempt++) {
      g_clear_pointer(&result->backup_path, g_free);
      g_autofree char *suffix =
          attempt == 0 ? g_strdup("") : g_strdup_printf("-%u", attempt);
      result->backup_path =
          g_strdup_printf("%s.shaula-backup-%" G_GINT64_FORMAT "%s",
                          result->path, (gint64)time(NULL), suffix);
      if (!g_file_test(result->backup_path, G_FILE_TEST_EXISTS))
        break;
    }
    if (!g_file_set_contents(result->backup_path, current,
                             (gssize)current_length, NULL))
      return FALSE;
  }
  g_autofree char *parent = g_path_get_dirname(result->path);
  if (g_mkdir_with_parents(parent, 0700) != 0)
    return FALSE;
  g_autofree char *temporary =
      g_strdup_printf("%s.shaula-tmp.%ld", result->path, (long)getpid());
  if (!g_file_set_contents(temporary, replacement, -1, NULL) ||
      g_rename(temporary, result->path) != 0) {
    (void)g_unlink(temporary);
    return FALSE;
  }
  return TRUE;
}

gboolean remove_managed_block(const char *path_override,
                              const char *begin_marker,
                              const char *end_marker, gboolean dry_run,
                              ManagedBlockResult *result, gboolean *invalid) {
  *result = (ManagedBlockResult){0};
  *invalid = FALSE;
  result->path = niri_config_path(path_override);
  if (result->path == NULL || begin_marker == NULL || end_marker == NULL)
    return FALSE;
  g_autofree char *current = NULL;
  gsize length = 0;
  if (!g_file_get_contents(result->path, &current, &length, NULL))
    return FALSE;
  guint begins = count_text(current, begin_marker);
  guint ends = count_text(current, end_marker);
  const char *begin = strstr(current, begin_marker);
  const char *end = strstr(current, end_marker);
  if (begins > 1 || ends > 1 || begins != ends ||
      (begin != NULL && end < begin)) {
    *invalid = TRUE;
    return FALSE;
  }
  if (begin == NULL)
    return TRUE;
  const char *suffix = end + strlen(end_marker);
  if (*suffix == '\n')
    suffix++;
  g_autofree char *replacement =
      g_strdup_printf("%.*s%s", (int)(begin - current), current, suffix);
  result->changed = TRUE;
  result->replaced = TRUE;
  if (dry_run)
    return TRUE;
  result->backup_path =
      g_strdup_printf("%s.shaula-backup-%" G_GINT64_FORMAT, result->path,
                      (gint64)time(NULL));
  if (!g_file_set_contents(result->backup_path, current, (gssize)length,
                           NULL))
    return FALSE;
  g_autofree char *temporary =
      g_strdup_printf("%s.shaula-tmp.%ld", result->path, (long)getpid());
  if (!g_file_set_contents(temporary, replacement, -1, NULL) ||
      g_rename(temporary, result->path) != 0) {
    (void)g_unlink(temporary);
    return FALSE;
  }
  return TRUE;
}
