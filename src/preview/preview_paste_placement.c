#include "preview_paste_placement.h"

#include <math.h>
#include <string.h>

static gboolean finite_positive(double value) {
  return isfinite(value) && value > 0.0;
}

static gboolean rect_is_finite(ShaulaRect rect) {
  return isfinite(rect.x) && isfinite(rect.y) && isfinite(rect.width) &&
         isfinite(rect.height);
}

static ShaulaRect rect_intersection(ShaulaRect a, ShaulaRect b) {
  a = shaula_rect_normalized(a);
  b = shaula_rect_normalized(b);
  double x0 = MAX(a.x, b.x);
  double y0 = MAX(a.y, b.y);
  double x1 = MIN(a.x + a.width, b.x + b.width);
  double y1 = MIN(a.y + a.height, b.y + b.height);
  return (ShaulaRect){x0, y0, MAX(0.0, x1 - x0), MAX(0.0, y1 - y0)};
}

static ShaulaRect inset_if_possible(ShaulaRect rect, double margin) {
  rect = shaula_rect_normalized(rect);
  if (!isfinite(margin) || margin <= 0.0)
    return rect;
  double inset_x = MIN(margin, rect.width / 4.0);
  double inset_y = MIN(margin, rect.height / 4.0);
  ShaulaRect inset = {rect.x + inset_x, rect.y + inset_y,
                      rect.width - 2.0 * inset_x,
                      rect.height - 2.0 * inset_y};
  return shaula_rect_is_empty(inset) ? rect : inset;
}

gboolean shaula_paste_calculate_placement(double content_width,
                                          double content_height,
                                          ShaulaRect viewport_image,
                                          ShaulaRect image_bounds,
                                          double margin,
                                          ShaulaPastePlacement *placement) {
  if (placement == NULL || !finite_positive(content_width) ||
      !finite_positive(content_height) || !rect_is_finite(viewport_image) ||
      !rect_is_finite(image_bounds))
    return FALSE;

  image_bounds = shaula_rect_normalized(image_bounds);
  viewport_image = shaula_rect_normalized(viewport_image);
  if (shaula_rect_is_empty(image_bounds) || shaula_rect_is_empty(viewport_image))
    return FALSE;

  ShaulaRect visible = rect_intersection(viewport_image, image_bounds);
  if (shaula_rect_is_empty(visible))
    visible = image_bounds;
  ShaulaRect available = inset_if_possible(visible, margin);
  if (shaula_rect_is_empty(available))
    return FALSE;

  double scale = MIN(1.0, MIN(available.width / content_width,
                              available.height / content_height));
  if (!finite_positive(scale))
    return FALSE;

  double width = content_width * scale;
  double height = content_height * scale;
  double x = available.x + (available.width - width) / 2.0;
  double y = available.y + (available.height - height) / 2.0;
  if (!isfinite(x) || !isfinite(y) || !finite_positive(width) ||
      !finite_positive(height))
    return FALSE;

  *placement = (ShaulaPastePlacement){x, y, width, height, scale};
  return TRUE;
}

ShaulaPoint shaula_paste_clamp_bounds_translation(ShaulaRect content_bounds,
                                                  ShaulaRect image_bounds,
                                                  double margin) {
  if (!rect_is_finite(content_bounds) || !rect_is_finite(image_bounds))
    return (ShaulaPoint){0, 0};
  content_bounds = shaula_rect_normalized(content_bounds);
  image_bounds = shaula_rect_normalized(image_bounds);
  if (shaula_rect_is_empty(content_bounds) || shaula_rect_is_empty(image_bounds))
    return (ShaulaPoint){0, 0};

  if (!isfinite(margin) || margin < 0.0)
    margin = 0.0;
  double inset_x = MIN(margin, image_bounds.width / 4.0);
  double inset_y = MIN(margin, image_bounds.height / 4.0);
  double left = image_bounds.x + inset_x;
  double top = image_bounds.y + inset_y;
  double right = image_bounds.x + image_bounds.width - inset_x;
  double bottom = image_bounds.y + image_bounds.height - inset_y;

  double dx = 0.0;
  double dy = 0.0;
  if (content_bounds.width >= right - left)
    dx = left - content_bounds.x;
  else if (content_bounds.x < left)
    dx = left - content_bounds.x;
  else if (content_bounds.x + content_bounds.width > right)
    dx = right - (content_bounds.x + content_bounds.width);

  if (content_bounds.height >= bottom - top)
    dy = top - content_bounds.y;
  else if (content_bounds.y < top)
    dy = top - content_bounds.y;
  else if (content_bounds.y + content_bounds.height > bottom)
    dy = bottom - (content_bounds.y + content_bounds.height);

  return (ShaulaPoint){dx, dy};
}

ShaulaPasteTextValidation shaula_paste_validate_text(const char *text,
                                                      gsize max_bytes) {
  if (text == NULL)
    return SHAULA_PASTE_TEXT_EMPTY;
  gsize length = strlen(text);
  if (length > max_bytes)
    return SHAULA_PASTE_TEXT_TOO_LARGE;
  if (!g_utf8_validate(text, (gssize)length, NULL))
    return SHAULA_PASTE_TEXT_INVALID_UTF8;

  for (const char *cursor = text; *cursor != '\0';
       cursor = g_utf8_next_char(cursor)) {
    if (!g_unichar_isspace(g_utf8_get_char(cursor)))
      return SHAULA_PASTE_TEXT_VALID;
  }
  return SHAULA_PASTE_TEXT_EMPTY;
}

gboolean shaula_paste_validate_image_dimensions(int width, int height,
                                                 int max_dimension,
                                                 gint64 max_pixels) {
  if (width <= 0 || height <= 0 || max_dimension <= 0 || max_pixels <= 0)
    return FALSE;
  if (width > max_dimension || height > max_dimension)
    return FALSE;
  return (gint64)width * (gint64)height <= max_pixels;
}
