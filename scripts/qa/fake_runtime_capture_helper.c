#include <errno.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gboolean parse_size(const char *geometry, int *width, int *height) {
  const char *space = geometry != NULL ? strchr(geometry, ' ') : NULL;
  const char *separator = space != NULL ? strchr(space + 1, 'x') : NULL;
  if (separator == NULL)
    return FALSE;
  errno = 0;
  char *end = NULL;
  long parsed_width = strtol(space + 1, &end, 10);
  if (errno != 0 || end != separator || parsed_width <= 0 ||
      parsed_width > G_MAXINT)
    return FALSE;
  long parsed_height = strtol(separator + 1, &end, 10);
  if (errno != 0 || *end != '\0' || parsed_height <= 0 ||
      parsed_height > G_MAXINT)
    return FALSE;
  *width = (int)parsed_width;
  *height = (int)parsed_height;
  return TRUE;
}

int main(int argc, char **argv) {
  const char *mode = NULL;
  const char *geometry = NULL;
  const char *output = NULL;
  for (int i = 1; i < argc; i++) {
    if (g_str_equal(argv[i], "--mode") && i + 1 < argc)
      mode = argv[++i];
    else if (g_str_equal(argv[i], "--geometry") && i + 1 < argc)
      geometry = argv[++i];
    else if (g_str_equal(argv[i], "--output") && i + 1 < argc)
      output = argv[++i];
    else if (g_str_equal(argv[i], "--backend") && i + 1 < argc)
      i++;
  }
  if (mode == NULL || output == NULL)
    return 2;

  int width = 1920;
  int height = 1080;
  if (g_str_equal(mode, "area")) {
    width = 640;
    height = 360;
    (void)parse_size(geometry, &width, &height);
  } else if (g_str_equal(mode, "window")) {
    width = 1280;
    height = 720;
  }

  g_autofree char *parent = g_path_get_dirname(output);
  if (g_mkdir_with_parents(parent, 0755) != 0)
    return 3;
  g_autoptr(GdkPixbuf) image =
      gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
  if (image == NULL)
    return 4;
  gdk_pixbuf_fill(image, 0x286090ffU);
  g_autoptr(GError) error = NULL;
  if (!gdk_pixbuf_save(image, output, "png", &error, NULL)) {
    fprintf(stderr, "%s\n", error != NULL ? error->message : "save failed");
    return 5;
  }
  return 0;
}
