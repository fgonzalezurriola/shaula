#include "recent_captures.h"

#include <gio/gio.h>
#include <glib/gstdio.h>

#define SHAULA_RECENT_MAX_ITEMS 12u
#define SHAULA_RECENT_MAX_BYTES (G_GUINT64_CONSTANT(256) * 1024u * 1024u)

typedef struct {
  char *path;
  gint64 modified;
  guint64 size;
} RecentEntry;

static void recent_entry_free(gpointer data) {
  RecentEntry *entry = data;
  if (entry == NULL)
    return;
  g_free(entry->path);
  g_free(entry);
}

static char *recent_directory(void) {
  const char *cache = g_get_user_cache_dir();
  if (cache == NULL || cache[0] == '\0')
    return NULL;
  return g_build_filename(cache, "shaula", "recent-captures", NULL);
}

static gint compare_recent_newest(gconstpointer left, gconstpointer right) {
  const RecentEntry *a = *(RecentEntry *const *)left;
  const RecentEntry *b = *(RecentEntry *const *)right;
  if (a->modified > b->modified)
    return -1;
  if (a->modified < b->modified)
    return 1;
  return g_strcmp0(a->path, b->path);
}

static GPtrArray *entries_load(const char *directory, GError **error) {
  /* Loads only regular PNG files so cache metadata cannot expose arbitrary
   * paths as composition sources. */
  GPtrArray *entries = g_ptr_array_new_with_free_func(recent_entry_free);
  g_autoptr(GDir) dir = g_dir_open(directory, 0, error);
  if (dir == NULL)
    return entries;

  const char *name = NULL;
  while ((name = g_dir_read_name(dir)) != NULL) {
    if (!g_str_has_suffix(name, ".png"))
      continue;
    g_autofree char *path = g_build_filename(directory, name, NULL);
    GStatBuf file_stat = {0};
    if (g_stat(path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode))
      continue;
    RecentEntry *entry = g_new0(RecentEntry, 1);
    entry->path = g_steal_pointer(&path);
    entry->modified = (gint64)file_stat.st_mtime;
    entry->size = file_stat.st_size > 0 ? (guint64)file_stat.st_size : 0;
    g_ptr_array_add(entries, entry);
  }
  g_ptr_array_sort(entries, compare_recent_newest);
  return entries;
}

static void prune_entries(GPtrArray *entries) {
  guint64 retained_bytes = 0;
  for (guint i = 0; i < entries->len; i++) {
    RecentEntry *entry = g_ptr_array_index(entries, i);
    gboolean keep = i < SHAULA_RECENT_MAX_ITEMS &&
                    entry->size <= SHAULA_RECENT_MAX_BYTES - retained_bytes;
    if (keep) {
      retained_bytes += entry->size;
      continue;
    }
    (void)g_unlink(entry->path);
  }
}

gboolean shaula_recent_captures_register(const char *source, GError **error) {
  /* Runtime boundary: retain only captures produced by this invocation and
   * keep the cache bounded before it becomes a Preview composition source. */
  if (source == NULL || !g_file_test(source, G_FILE_TEST_IS_REGULAR)) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT,
                "Recent capture source is unavailable.");
    return FALSE;
  }
  g_autofree char *directory = recent_directory();
  if (directory == NULL || g_mkdir_with_parents(directory, 0700) != 0) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                "Recent capture cache is unavailable.");
    return FALSE;
  }

  g_autofree char *destination = NULL;
  for (guint attempt = 0; attempt < 1000 && destination == NULL; attempt++) {
    g_autofree char *name =
        g_strdup_printf("capture-%" G_GINT64_FORMAT "-%u.png",
                        g_get_real_time(), attempt);
    g_autofree char *candidate = g_build_filename(directory, name, NULL);
    if (!g_file_test(candidate, G_FILE_TEST_EXISTS))
      destination = g_steal_pointer(&candidate);
  }
  if (destination == NULL) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_EXIST,
                "Recent capture cache could not allocate a filename.");
    return FALSE;
  }

  g_autoptr(GFile) source_file = g_file_new_for_path(source);
  g_autoptr(GFile) destination_file = g_file_new_for_path(destination);
  if (!g_file_copy(source_file, destination_file, G_FILE_COPY_NONE, NULL, NULL,
                   NULL, error))
    return FALSE;

  g_autoptr(GPtrArray) entries = entries_load(directory, NULL);
  prune_entries(entries);
  return TRUE;
}

GPtrArray *shaula_recent_captures_list(GError **error) {
  /* Runtime boundary: prune before returning private cache paths and return a
   * caller-owned array even when the cache has not been created yet. */
  g_autofree char *directory = recent_directory();
  if (directory == NULL) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                "Recent capture cache is unavailable.");
    return g_ptr_array_new_with_free_func(g_free);
  }
  if (!g_file_test(directory, G_FILE_TEST_EXISTS))
    return g_ptr_array_new_with_free_func(g_free);

  g_autoptr(GPtrArray) entries = entries_load(directory, error);
  prune_entries(entries);
  GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
  guint64 retained_bytes = 0;
  for (guint i = 0; i < entries->len && paths->len < SHAULA_RECENT_MAX_ITEMS;
       i++) {
    RecentEntry *entry = g_ptr_array_index(entries, i);
    if (!g_file_test(entry->path, G_FILE_TEST_IS_REGULAR) ||
        entry->size > SHAULA_RECENT_MAX_BYTES - retained_bytes)
      continue;
    retained_bytes += entry->size;
    g_ptr_array_add(paths, g_strdup(entry->path));
  }
  return paths;
}
