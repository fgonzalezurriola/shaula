#include "preview_image_io.h"

#include <string.h>

#include <glib/gstdio.h>

gboolean shaula_image_io_copy_file_bytes(const char *source, const char *target,
                                         GError **error) {
  if (source == NULL || target == NULL)
    return FALSE;

  g_autofree gchar *contents = NULL;
  gsize length = 0;
  if (!g_file_get_contents(source, &contents, &length, error))
    return FALSE;

  if (length > G_MAXSSIZE) {
    g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                        "File is too large to copy");
    return FALSE;
  }

  return g_file_set_contents(target, contents, (gssize)length, error);
}

gboolean shaula_image_io_path_has_png_extension(const char *path) {
  if (path == NULL)
    return FALSE;

  const char *dot = strrchr(path, '.');
  return dot != NULL && g_ascii_strcasecmp(dot, ".png") == 0;
}

char *shaula_image_io_with_png_extension(const char *path) {
  if (path == NULL)
    return NULL;
  if (shaula_image_io_path_has_png_extension(path))
    return g_strdup(path);
  return g_strconcat(path, ".png", NULL);
}

gboolean shaula_image_io_open_containing_folder(const char *path,
                                                GError **error) {
  if (path == NULL || path[0] == '\0')
    return FALSE;

  g_autofree gchar *directory = g_path_get_dirname(path);
  if (directory == NULL)
    return FALSE;

  gchar *argv[] = {(gchar *)"xdg-open", directory, NULL};
  return g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
                       error);
}
