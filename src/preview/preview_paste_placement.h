#ifndef SHAULA_PREVIEW_PASTE_PLACEMENT_H
#define SHAULA_PREVIEW_PASTE_PLACEMENT_H

#include <glib.h>

#include "preview_geometry.h"

typedef struct {
  double x;
  double y;
  double width;
  double height;
  double scale;
} ShaulaPastePlacement;

typedef enum {
  SHAULA_PASTE_TEXT_VALID,
  SHAULA_PASTE_TEXT_EMPTY,
  SHAULA_PASTE_TEXT_TOO_LARGE,
  SHAULA_PASTE_TEXT_INVALID_UTF8,
} ShaulaPasteTextValidation;

/* Computes a finite, aspect-preserving placement inside the visible portion of
 * the base image. Content is never scaled above 1.0.
 */
gboolean shaula_paste_calculate_placement(double content_width,
                                          double content_height,
                                          ShaulaRect viewport_image,
                                          ShaulaRect image_bounds,
                                          double margin,
                                          ShaulaPastePlacement *placement);

/* Returns the translation needed to keep a pasted annotation's leading bounds
 * inside the image. Oversized content remains unscaled and anchored at margin.
 */
ShaulaPoint shaula_paste_clamp_bounds_translation(ShaulaRect content_bounds,
                                                  ShaulaRect image_bounds,
                                                  double margin);

ShaulaPasteTextValidation shaula_paste_validate_text(const char *text,
                                                      gsize max_bytes);
gboolean shaula_paste_validate_image_dimensions(int width, int height,
                                                 int max_dimension,
                                                 gint64 max_pixels);

#endif
