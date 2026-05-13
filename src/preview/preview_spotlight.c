#include "preview_spotlight.h"

static void append_spotlight_path(cairo_t *cr, ShaulaSpotlightRegion region) {
  ShaulaRect rect = region.rect;
  if (region.shape == SHAULA_SPOTLIGHT_SHAPE_ROUNDED_RECTANGLE) {
    double r = MIN(rect.width, rect.height) * 0.12;
    r = CLAMP(r, 4.0, 18.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, rect.x + rect.width - r, rect.y + r, r, -0.5 * G_PI, 0);
    cairo_arc(cr, rect.x + rect.width - r, rect.y + rect.height - r, r, 0,
              0.5 * G_PI);
    cairo_arc(cr, rect.x + r, rect.y + rect.height - r, r, 0.5 * G_PI, G_PI);
    cairo_arc(cr, rect.x + r, rect.y + r, r, G_PI, 1.5 * G_PI);
    cairo_close_path(cr);
    return;
  }
  cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
}

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
    ShaulaSpotlightRegion region =
        g_array_index(state->spotlight_regions, ShaulaSpotlightRegion, i);
    region.rect = shaula_rect_clamped_c(region.rect,
                                        shaula_preview_image_width(state),
                                        shaula_preview_image_height(state));
    if (shaula_rect_is_empty(region.rect))
      continue;
    append_spotlight_path(cr, region);
    cairo_fill(cr);
  }

  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_pop_group_to_source(cr);
  cairo_paint(cr);
  cairo_restore(cr);

  for (guint i = 0; i < state->spotlight_regions->len; i++) {
    ShaulaSpotlightRegion region =
        g_array_index(state->spotlight_regions, ShaulaSpotlightRegion, i);
    region.rect = shaula_rect_clamped_c(region.rect,
                                        shaula_preview_image_width(state),
                                        shaula_preview_image_height(state));
    if (shaula_rect_is_empty(region.rect) || region.border_width <= 0.0)
      continue;
    cairo_save(cr);
    append_spotlight_path(cr, region);
    cairo_set_source_rgba(cr, region.border_color.r, region.border_color.g,
                          region.border_color.b, region.border_color.a);
    cairo_set_line_width(cr, region.border_width);
    cairo_stroke(cr);
    cairo_restore(cr);
  }
}
