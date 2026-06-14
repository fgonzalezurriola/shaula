#include <errno.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static gboolean parse_int(const char *text, int *out) {
  if (text == NULL || *text == '\0')
    return FALSE;
  errno = 0;
  char *end = NULL;
  long value = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value < G_MININT ||
      value > G_MAXINT)
    return FALSE;
  *out = (int)value;
  return TRUE;
}

static int clamp_int(int value, int low, int high) {
  if (high < low)
    return low;
  if (value < low)
    return low;
  if (value > high)
    return high;
  return value;
}

int main(int argc, char **argv) {
  if (argc != 9) {
    fprintf(stderr,
            "usage: shaula-crop-image <source> <output> <x> <y> <w> <h> "
            "<surface-w> <surface-h>\n");
    return 2;
  }

  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int surface_w = 0;
  int surface_h = 0;
  if (!parse_int(argv[3], &x) || !parse_int(argv[4], &y) ||
      !parse_int(argv[5], &w) || !parse_int(argv[6], &h) ||
      !parse_int(argv[7], &surface_w) || !parse_int(argv[8], &surface_h) ||
      w <= 0 || h <= 0 || surface_w <= 0 || surface_h <= 0) {
    fprintf(stderr, "invalid crop geometry\n");
    return 3;
  }

  GError *error = NULL;
  GdkPixbuf *source = gdk_pixbuf_new_from_file(argv[1], &error);
  if (source == NULL) {
    fprintf(stderr, "could not load source image: %s\n",
            error != NULL ? error->message : "unknown error");
    if (error != NULL)
      g_error_free(error);
    return 4;
  }

  int image_w = gdk_pixbuf_get_width(source);
  int image_h = gdk_pixbuf_get_height(source);
  int left = (int)floor(((double)x * (double)image_w) / (double)surface_w);
  int top = (int)floor(((double)y * (double)image_h) / (double)surface_h);
  int right =
      (int)ceil(((double)(x + w) * (double)image_w) / (double)surface_w);
  int bottom =
      (int)ceil(((double)(y + h) * (double)image_h) / (double)surface_h);

  left = clamp_int(left, 0, image_w - 1);
  top = clamp_int(top, 0, image_h - 1);
  right = clamp_int(right, left + 1, image_w);
  bottom = clamp_int(bottom, top + 1, image_h);

  GdkPixbuf *sub = gdk_pixbuf_new_subpixbuf(source, left, top, right - left,
                                            bottom - top);
  if (sub == NULL) {
    g_object_unref(source);
    fprintf(stderr, "could not crop source image\n");
    return 5;
  }

  GdkPixbuf *copy = gdk_pixbuf_copy(sub);
  g_object_unref(sub);
  if (copy == NULL) {
    g_object_unref(source);
    fprintf(stderr, "could not copy cropped image\n");
    return 6;
  }

  gboolean saved = gdk_pixbuf_save(copy, argv[2], "png", &error, NULL);
  g_object_unref(copy);
  g_object_unref(source);
  if (!saved) {
    fprintf(stderr, "could not save cropped image: %s\n",
            error != NULL ? error->message : "unknown error");
    if (error != NULL)
      g_error_free(error);
    return 7;
  }

  return 0;
}
