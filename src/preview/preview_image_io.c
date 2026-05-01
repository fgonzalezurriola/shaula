#include "preview_image_io.h"

#include <string.h>

gboolean shaula_image_io_copy_file_bytes(const char *source,
                                         const char *target, GError **error) {
  gchar *contents = NULL;
  gsize len = 0;
  if (!g_file_get_contents(source, &contents, &len, error))
    return FALSE;
  gboolean ok = g_file_set_contents(target, contents, len, error);
  g_free(contents);
  return ok;
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
  return g_strdup_printf("%s.png", path);
}

gboolean shaula_image_io_open_containing_folder(const char *path,
                                                GError **error) {
  if (path == NULL || path[0] == '\0')
    return FALSE;
  char *dir = g_path_get_dirname(path);
  char *quoted = g_shell_quote(dir);
  char *command = g_strdup_printf("xdg-open %s", quoted);
  gboolean ok = g_spawn_command_line_async(command, error);
  g_free(command);
  g_free(quoted);
  g_free(dir);
  return ok;
}
