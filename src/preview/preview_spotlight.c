#include "preview_spotlight.h"

void shaula_preview_draw_spotlight_effect(ShaulaPreviewState *state,
                                          cairo_t *cr) {
  if (state == NULL || cr == NULL || state->spotlight_regions == NULL ||
      state->spotlight_regions->len == 0)
    return;

  cairo_save(cr);
  cairo_push_group(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
  cairo_rectangle(cr, 0, 0, shaula_preview_image_width(state),
                  shaula_preview_image_height(state));
  cairo_fill(cr);

  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  for (guint i = 0; i < state->spotlight_regions->len; i++) {
    ShaulaRect rect =
        g_array_index(state->spotlight_regions, ShaulaRect, i);
    rect = shaula_rect_clamped(rect, shaula_preview_image_width(state),
                               shaula_preview_image_height(state));
    if (shaula_rect_is_empty(rect))
      continue;
    cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
    cairo_fill(cr);
  }

  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_pop_group_to_source(cr);
  cairo_paint(cr);
  cairo_restore(cr);
}
