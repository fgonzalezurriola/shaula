#include "preview_render.h"

#include <cairo.h>
#include <gdk/gdk.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include "preview_spotlight.h"

static GQuark render_error_quark(void) {
  return g_quark_from_static_string("shaula-preview-render");
}

/* Runtime boundary: render the current preview state into a temporary PNG used
 * by both copy and save. The output is the base image currently visible in the
 * canvas plus vector annotations and document effects in image coordinates.
 */
char *shaula_render_composited_png_temp(ShaulaPreviewState *state,
                                        GError **error) {
  if (state == NULL || state->image == NULL) {
    g_set_error(error, render_error_quark(), 1, "preview image is missing");
    return NULL;
  }

  int width = shaula_preview_image_width(state);
  int height = shaula_preview_image_height(state);
  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    g_set_error(error, render_error_quark(), 2, "failed to create render surface");
    cairo_surface_destroy(surface);
    return NULL;
  }

  cairo_t *cr = cairo_create(surface);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  gdk_cairo_set_source_pixbuf(cr, state->image, 0, 0);
  cairo_paint(cr);

  cairo_save(cr);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_clip(cr);
  shaula_preview_draw_spotlight_effect(state, cr);
  for (guint i = 0; state->annotations != NULL && i < state->annotations->len;
       i++) {
    ShaulaAnnotation *annotation = g_ptr_array_index(state->annotations, i);
    gboolean selected = annotation->selected;
    annotation->selected = FALSE;
    shaula_annotation_draw(cr, annotation);
    annotation->selected = selected;
  }
  cairo_restore(cr);

  cairo_destroy(cr);

  char *path = g_strdup_printf("%s/shaula-preview-XXXXXX.png", g_get_tmp_dir());
  int fd = g_mkstemp(path);
  if (fd < 0) {
    g_set_error(error, render_error_quark(), 3,
                "failed to create temporary PNG path");
    cairo_surface_destroy(surface);
    g_free(path);
    return NULL;
  }
  close(fd);

  cairo_status_t status = cairo_surface_write_to_png(surface, path);
  cairo_surface_destroy(surface);
  if (status != CAIRO_STATUS_SUCCESS) {
    g_set_error(error, render_error_quark(), 4, "failed to write PNG: %s",
                cairo_status_to_string(status));
    g_unlink(path);
    g_free(path);
    return NULL;
  }
  return path;
}
