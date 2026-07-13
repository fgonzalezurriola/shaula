#include "preview_state.h"

#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#include "preview_document_edit.h"
#include "preview_paths.h"
#include "preview_system_clipboard.h"
#include "preview_toolbar.h"

#define SHAULA_CROP_MIN_SIZE_PX 4
#define SHAULA_ERASE_COLOR_BUCKETS_PER_CHANNEL 16
#define SHAULA_ERASE_COLOR_BUCKET_COUNT                                        \
  (SHAULA_ERASE_COLOR_BUCKETS_PER_CHANNEL *                                    \
   SHAULA_ERASE_COLOR_BUCKETS_PER_CHANNEL *                                    \
   SHAULA_ERASE_COLOR_BUCKETS_PER_CHANNEL)

static gboolean detect_dark_theme(void) {
  GtkSettings *settings = gtk_settings_get_default();
  if (settings == NULL)
    return TRUE;
  gchar *theme = NULL;
  g_object_get(settings, "gtk-theme-name", &theme, NULL);
  if (theme != NULL) {
    gboolean light = g_str_has_suffix(theme, "-light") ||
                     g_str_has_suffix(theme, "-Light") ||
                     g_strrstr(theme, "Latte") != NULL ||
                     g_strrstr(theme, "latte") != NULL ||
                     g_strrstr(theme, "Light") != NULL ||
                     g_strrstr(theme, "light") != NULL;
    gboolean dark =
        g_str_has_suffix(theme, "-dark") || g_str_has_suffix(theme, "-Dark") ||
        g_strrstr(theme, "Mocha") != NULL ||
        g_strrstr(theme, "mocha") != NULL ||
        g_strrstr(theme, "Macchiato") != NULL ||
        g_strrstr(theme, "macchiato") != NULL ||
        g_strrstr(theme, "Frappe") != NULL ||
        g_strrstr(theme, "frappe") != NULL ||
        g_strrstr(theme, "Nord") != NULL || g_strrstr(theme, "nord") != NULL;
    g_free(theme);
    if (light)
      return FALSE;
    if (dark)
      return TRUE;
  }
  gint pref = 0;
  g_object_get(settings, "gtk-application-prefer-dark-theme", &pref, NULL);
  if (pref)
    return TRUE;
  return FALSE;
}

void shaula_preview_state_init(ShaulaPreviewState *state, const char *path,
                               GdkPixbuf *image) {
  memset(state, 0, sizeof(*state));
  shaula_preview_document_init(&state->document, path, image);
  state->managed_temp_path =
      shaula_preview_path_is_temporary_capture(path) ? g_strdup(path) : NULL;
  state->zoom = 1.0;
  state->fit_zoom = 1.0;
  state->fit_mode = TRUE;
  state->active_tool = SHAULA_TOOL_SELECT;
  state->previous_tool_before_space_pan = SHAULA_TOOL_SELECT;
  state->previous_tool_before_eraser = SHAULA_TOOL_SELECT;
  state->operation = SHAULA_OPERATION_NONE;
  state->last_action = "close";
  state->is_dark = TRUE;
  state->current_color = shaula_color_default();
  state->hover_color_valid = FALSE;
  state->hover_color = state->current_color;
  shaula_color_to_hex(state->hover_color, state->hover_hex);
  shaula_tool_defaults_init(&state->tool_defaults);
  shaula_properties_hud_state_init(&state->properties_hud);
  state->draft_pen_points = g_array_new(FALSE, FALSE, sizeof(ShaulaPoint));
  state->eraser_pending_annotation_ids = g_array_new(FALSE, FALSE, sizeof(int));
  state->eraser_trail = g_array_new(FALSE, FALSE, sizeof(ShaulaEraserTrailPoint));
  shaula_annotation_editor_init(&state->annotation_editor);
  shaula_preview_gesture_state_init(&state->gesture);
  state->measure_tolerance = 32;
  state->toolbar_overflow_visible_count = -1;
}

void shaula_preview_state_free(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  if (state->feedback_timeout_id != 0)
    g_source_remove(state->feedback_timeout_id);
  state->feedback_timeout_id = 0;
  shaula_system_clipboard_paste_free(state->system_clipboard_paste);
  state->system_clipboard_paste = NULL;
  shaula_tool_defaults_flush(&state->tool_defaults);
  shaula_tool_defaults_dispose(&state->tool_defaults);
  if (state->eraser_tail_timeout_id != 0)
    g_source_remove(state->eraser_tail_timeout_id);
  if (state->draft_pen_points != NULL)
    g_array_unref(state->draft_pen_points);
  if (state->eraser_pending_annotation_ids != NULL)
    g_array_unref(state->eraser_pending_annotation_ids);
  if (state->eraser_trail != NULL)
    g_array_unref(state->eraser_trail);
  shaula_annotation_editor_dispose(&state->annotation_editor);
  for (int i = 0; i < state->icon_root_count; i++)
    g_free(state->icon_roots[i]);
  if (state->managed_temp_path != NULL &&
      !(state->document.copied && !state->document.saved && state->notified &&
        g_strcmp0(state->last_action, "copy") == 0))
    g_unlink(state->managed_temp_path);
  g_free(state->managed_temp_path);
  shaula_preview_document_free(&state->document);
}

gboolean shaula_preview_state_has_modifications(ShaulaPreviewState *state) {
  return state != NULL &&
         shaula_preview_document_has_modifications(&state->document);
}

int shaula_preview_image_width(ShaulaPreviewState *state) {
  return state != NULL ? shaula_preview_document_width(&state->document) : 0;
}

int shaula_preview_image_height(ShaulaPreviewState *state) {
  return state != NULL ? shaula_preview_document_height(&state->document) : 0;
}

void shaula_preview_update_theme_state(ShaulaPreviewState *state) {
  state->is_dark = detect_dark_theme();
}

void shaula_preview_queue_draw(ShaulaPreviewState *state) {
  if (state->area != NULL)
    gtk_widget_queue_draw(state->area);
}

static gboolean hide_feedback(gpointer data) {
  ShaulaPreviewState *state = data;
  state->feedback_timeout_id = 0;
  if (state->feedback_label != NULL)
    gtk_widget_set_visible(state->feedback_label, FALSE);
  return G_SOURCE_REMOVE;
}

void shaula_preview_show_feedback(ShaulaPreviewState *state,
                                  const char *message, gboolean is_error) {
  if (state == NULL || state->canvas_overlay == NULL || message == NULL)
    return;
  if (state->feedback_label == NULL) {
    GtkWidget *label = gtk_label_new(NULL);
    state->feedback_label = label;
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_top(label, 18);
    gtk_widget_set_margin_start(label, 24);
    gtk_widget_set_margin_end(label, 24);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 58);
    gtk_widget_add_css_class(label, "osd");
    gtk_widget_set_can_target(label, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(state->canvas_overlay), label);
  }

  gtk_label_set_text(GTK_LABEL(state->feedback_label), message);
  if (is_error)
    gtk_widget_add_css_class(state->feedback_label, "error");
  else
    gtk_widget_remove_css_class(state->feedback_label, "error");
  gtk_widget_set_visible(state->feedback_label, TRUE);
  if (state->feedback_timeout_id != 0)
    g_source_remove(state->feedback_timeout_id);
  state->feedback_timeout_id = g_timeout_add_seconds(4, hide_feedback, state);
}

void shaula_preview_update_dimensions_label(ShaulaPreviewState *state) {
  if (state->dimensions_label == NULL || state->document.image == NULL)
    return;
  char size_buf[32];
  snprintf(size_buf, sizeof(size_buf), "%dw\xc3\x97%dh",
           shaula_preview_image_width(state),
           shaula_preview_image_height(state));
  gtk_label_set_text(GTK_LABEL(state->dimensions_label), size_buf);
}

void shaula_preview_update_zoom_label(ShaulaPreviewState *state) {
  if (state->zoom_label == NULL)
    return;
  int pct = (int)(state->zoom * 100.0 + 0.5);
  char buf[32];
  snprintf(buf, sizeof(buf), "Zoom %d%%", pct);
  gtk_label_set_text(GTK_LABEL(state->zoom_label), buf);
}

void shaula_preview_update_fit_zoom(ShaulaPreviewState *state) {
  if (state->area == NULL || state->document.image == NULL)
    return;
  int area_w = MAX(1, gtk_widget_get_width(state->area));
  int area_h = MAX(1, gtk_widget_get_height(state->area));
  int image_w = MAX(1, shaula_preview_image_width(state));
  int image_h = MAX(1, shaula_preview_image_height(state));
  double scale_x = (double)MAX(1, area_w - 48) / (double)image_w;
  double scale_y = (double)MAX(1, area_h - 48) / (double)image_h;
  state->fit_zoom = MIN(1.0, MAX(0.05, MIN(scale_x, scale_y)));
  if (state->fit_mode) {
    state->zoom = state->fit_zoom;
    state->pan_x = ((double)area_w - (double)image_w * state->zoom) / 2.0;
    state->pan_y = ((double)area_h - (double)image_h * state->zoom) / 2.0;
  }
}

void shaula_preview_set_fit_mode(ShaulaPreviewState *state, gboolean fit) {
  state->fit_mode = fit;
  shaula_preview_update_fit_zoom(state);
  shaula_preview_update_zoom_label(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_actual_size(ShaulaPreviewState *state) {
  if (state->area == NULL || state->document.image == NULL)
    return;
  state->fit_mode = FALSE;
  state->zoom = 1.0;
  shaula_preview_update_fit_zoom(state);
  int area_w = MAX(1, gtk_widget_get_width(state->area));
  int area_h = MAX(1, gtk_widget_get_height(state->area));
  int image_w = MAX(1, shaula_preview_image_width(state));
  int image_h = MAX(1, shaula_preview_image_height(state));
  state->pan_x = ((double)area_w - (double)image_w) / 2.0;
  state->pan_y = ((double)area_h - (double)image_h) / 2.0;
  shaula_preview_update_zoom_label(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_zoom_by_factor(ShaulaPreviewState *state, double factor) {
  if (state->document.image == NULL || state->area == NULL)
    return;
  state->fit_mode = FALSE;
  double old_zoom = state->zoom;
  double next = CLAMP(old_zoom * factor, 0.05, 8.0);
  if (fabs(next - old_zoom) < 0.001)
    return;

  int area_w = MAX(1, gtk_widget_get_width(state->area));
  int area_h = MAX(1, gtk_widget_get_height(state->area));
  double cx = (double)area_w / 2.0;
  double cy = (double)area_h / 2.0;
  double image_cx = (cx - state->pan_x) / old_zoom;
  double image_cy = (cy - state->pan_y) / old_zoom;
  state->zoom = next;
  state->pan_x = cx - image_cx * state->zoom;
  state->pan_y = cy - image_cy * state->zoom;
  shaula_preview_update_zoom_label(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_clear_region_selection(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  state->has_region_selection = FALSE;
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_selection_state(state);
}

static gboolean pending_erase_ids_contain(ShaulaPreviewState *state, int id) {
  if (state == NULL || state->eraser_pending_annotation_ids == NULL || id <= 0)
    return FALSE;
  for (guint i = 0; i < state->eraser_pending_annotation_ids->len; i++) {
    if (g_array_index(state->eraser_pending_annotation_ids, int, i) == id)
      return TRUE;
  }
  return FALSE;
}

gboolean shaula_preview_is_annotation_pending_erase(
    ShaulaPreviewState *state, ShaulaAnnotation *annotation) {
  return annotation != NULL && pending_erase_ids_contain(state, annotation->id);
}

void shaula_preview_clear_eraser_pending(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  if (state->eraser_pending_annotation_ids != NULL)
    g_array_set_size(state->eraser_pending_annotation_ids, 0);
}

void shaula_preview_cancel_eraser_gesture(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  shaula_preview_clear_eraser_pending(state);
  state->eraser_drag_active = FALSE;
  state->eraser_tail_fading = FALSE;
  if (state->eraser_tail_timeout_id != 0) {
    g_source_remove(state->eraser_tail_timeout_id);
    state->eraser_tail_timeout_id = 0;
  }
  state->operation = SHAULA_OPERATION_NONE;
  if (state->eraser_trail != NULL)
    g_array_set_size(state->eraser_trail, 0);
  shaula_preview_queue_draw(state);
}

gboolean shaula_preview_commit_eraser_pending(ShaulaPreviewState *state) {
  if (state == NULL || state->document.annotations == NULL ||
      state->eraser_pending_annotation_ids == NULL ||
      state->eraser_pending_annotation_ids->len == 0)
    return FALSE;

  shaula_preview_push_undo(state);
  for (gint i = (gint)state->document.annotations->len - 1; i >= 0; i--) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->document.annotations, (guint)i);
    if (shaula_preview_is_annotation_pending_erase(state, annotation))
      shaula_preview_document_remove_annotation_at(&state->document, (guint)i);
  }
  shaula_preview_clear_eraser_pending(state);
  shaula_annotation_editor_clear_selection(state);
  shaula_preview_toolbar_update_history_state(state);
  return TRUE;
}

void shaula_preview_push_undo(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  shaula_preview_history_push_undo(
      &state->document.history,
      shaula_preview_document_snapshot_new(&state->document), TRUE);
  shaula_preview_toolbar_update_history_state(state);
}

void shaula_preview_begin_history_gesture(ShaulaPreviewState *state) {
  if (state == NULL || state->document.pending_history_snapshot != NULL)
    return;
  state->document.pending_history_snapshot =
      shaula_preview_document_snapshot_new(&state->document);
}

void shaula_preview_commit_history_gesture(ShaulaPreviewState *state,
                                           gboolean changed) {
  if (state == NULL || state->document.pending_history_snapshot == NULL)
    return;
  if (changed) {
    shaula_preview_history_push_undo(&state->document.history,
                                     state->document.pending_history_snapshot,
                                     TRUE);
    state->document.pending_history_snapshot = NULL;
    shaula_preview_toolbar_update_history_state(state);
    return;
  }
  shaula_preview_snapshot_free(state->document.pending_history_snapshot);
  state->document.pending_history_snapshot = NULL;
}

void shaula_preview_cancel_history_gesture(ShaulaPreviewState *state) {
  if (state == NULL || state->document.pending_history_snapshot == NULL)
    return;
  shaula_preview_snapshot_free(state->document.pending_history_snapshot);
  state->document.pending_history_snapshot = NULL;
}

gboolean shaula_preview_can_undo(ShaulaPreviewState *state) {
  return state != NULL && (state->document.pending_history_snapshot != NULL ||
                           shaula_preview_history_can_undo(
                               &state->document.history));
}

gboolean shaula_preview_can_redo(ShaulaPreviewState *state) {
  return state != NULL &&
         shaula_preview_history_can_redo(&state->document.history);
}

static void restore_snapshot(ShaulaPreviewState *state,
                             ShaulaPreviewSnapshot *snapshot) {
  shaula_preview_document_restore_snapshot(&state->document, snapshot);
  shaula_annotation_editor_rebuild_selection(state);
  state->has_crop_draft = FALSE;
  state->has_region_selection = FALSE;
  state->operation = SHAULA_OPERATION_NONE;
  shaula_preview_cancel_eraser_gesture(state);
  shaula_properties_hud_set_panel(&state->properties_hud,
                                  SHAULA_PROPERTIES_PANEL_NONE);
  if (state->text_entry != NULL) {
    if (state->canvas_overlay != NULL)
      gtk_overlay_remove_overlay(GTK_OVERLAY(state->canvas_overlay),
                                 state->text_entry);
    else
      gtk_widget_unparent(state->text_entry);
    state->text_entry = NULL;
  }
  state->text_editing_id = 0;
  if (state->draft_pen_points != NULL)
    g_array_set_size(state->draft_pen_points, 0);
  shaula_preview_cancel_history_gesture(state);
  shaula_preview_update_dimensions_label(state);
  shaula_preview_set_fit_mode(state, TRUE);
}

gboolean shaula_preview_undo(ShaulaPreviewState *state) {
  if (state == NULL)
    return FALSE;
  shaula_preview_commit_history_gesture(state, TRUE);
  if (!shaula_preview_can_undo(state))
    return FALSE;
  ShaulaPreviewSnapshot *snapshot =
      shaula_preview_history_pop_undo(&state->document.history);
  shaula_preview_history_push_redo(
      &state->document.history,
      shaula_preview_document_snapshot_new(&state->document));
  restore_snapshot(state, snapshot);
  shaula_preview_snapshot_free(snapshot);
  shaula_preview_toolbar_update_history_state(state);
  return TRUE;
}

gboolean shaula_preview_redo(ShaulaPreviewState *state) {
  if (state == NULL)
    return FALSE;
  shaula_preview_commit_history_gesture(state, TRUE);
  if (!shaula_preview_can_redo(state))
    return FALSE;
  ShaulaPreviewSnapshot *snapshot =
      shaula_preview_history_pop_redo(&state->document.history);
  shaula_preview_history_push_undo(
      &state->document.history,
      shaula_preview_document_snapshot_new(&state->document), FALSE);
  restore_snapshot(state, snapshot);
  shaula_preview_snapshot_free(snapshot);
  shaula_preview_toolbar_update_history_state(state);
  return TRUE;
}

void shaula_preview_cancel_operation(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  if (state->operation == SHAULA_OPERATION_ERASE_ANNOTATIONS)
    shaula_preview_cancel_eraser_gesture(state);
  state->operation = SHAULA_OPERATION_NONE;
  state->operation_changed = FALSE;
  shaula_preview_gesture_reset(&state->gesture);
  shaula_preview_cancel_history_gesture(state);
  state->has_crop_draft = FALSE;
  state->has_region_selection = FALSE;
  shaula_properties_hud_set_panel(&state->properties_hud,
                                  SHAULA_PROPERTIES_PANEL_NONE);
  if (state->draft_pen_points != NULL)
    g_array_set_size(state->draft_pen_points, 0);
  if (state->text_entry != NULL) {
    if (state->canvas_overlay != NULL)
      gtk_overlay_remove_overlay(GTK_OVERLAY(state->canvas_overlay),
                                 state->text_entry);
    else
      gtk_widget_unparent(state->text_entry);
    state->text_entry = NULL;
  }
  state->text_editing_id = 0;
  shaula_preview_queue_draw(state);
}

static gboolean crop_rect_to_pixels(ShaulaPreviewState *state, ShaulaRect rect,
                                    int *x, int *y, int *w, int *h) {
  if (state == NULL || state->document.image == NULL)
    return FALSE;

  ShaulaRect crop =
      shaula_rect_clamped_c(rect, shaula_preview_image_width(state),
                            shaula_preview_image_height(state));
  if (crop.width < SHAULA_CROP_MIN_SIZE_PX ||
      crop.height < SHAULA_CROP_MIN_SIZE_PX)
    return FALSE;

  *x = (int)floor(crop.x);
  *y = (int)floor(crop.y);
  *w = MAX(1, (int)ceil(crop.x + crop.width) - *x);
  *h = MAX(1, (int)ceil(crop.y + crop.height) - *y);
  *w = MIN(*w, shaula_preview_image_width(state) - *x);
  *h = MIN(*h, shaula_preview_image_height(state) - *y);
  return *w >= SHAULA_CROP_MIN_SIZE_PX && *h >= SHAULA_CROP_MIN_SIZE_PX;
}

typedef enum {
  SHAULA_REGION_EDIT_BLUR,
  SHAULA_REGION_EDIT_ERASE
} ShaulaRegionEdit;

static gboolean pixbuf_require_rgb8(GdkPixbuf *pixbuf, guchar **pixels,
                                    int *width, int *height, int *rowstride,
                                    int *channels) {
  if (pixbuf == NULL || gdk_pixbuf_get_bits_per_sample(pixbuf) != 8)
    return FALSE;
  *pixels = gdk_pixbuf_get_pixels(pixbuf);
  *width = gdk_pixbuf_get_width(pixbuf);
  *height = gdk_pixbuf_get_height(pixbuf);
  *rowstride = gdk_pixbuf_get_rowstride(pixbuf);
  *channels = gdk_pixbuf_get_n_channels(pixbuf);
  return *pixels != NULL && (*channels == 3 || *channels == 4);
}

static void pixelate_region(GdkPixbuf *pixbuf, int x, int y, int w, int h) {
  guchar *pixels = NULL;
  int width = 0;
  int height = 0;
  int rowstride = 0;
  int channels = 0;
  if (!pixbuf_require_rgb8(pixbuf, &pixels, &width, &height, &rowstride,
                           &channels))
    return;

  const int block = 12;
  for (int by = y; by < y + h; by += block) {
    int bh = MIN(block, y + h - by);
    for (int bx = x; bx < x + w; bx += block) {
      int bw = MIN(block, x + w - bx);
      guint64 r = 0;
      guint64 g = 0;
      guint64 b = 0;
      guint64 a = 0;
      int count = 0;
      for (int py = by; py < by + bh; py++) {
        guchar *row = pixels + py * rowstride;
        for (int px = bx; px < bx + bw; px++) {
          guchar *p = row + px * channels;
          r += p[0];
          g += p[1];
          b += p[2];
          if (channels == 4)
            a += p[3];
          count++;
        }
      }
      if (count == 0)
        continue;
      guint8 ar = (guint8)(r / count);
      guint8 ag = (guint8)(g / count);
      guint8 ab = (guint8)(b / count);
      guint8 aa = channels == 4 ? (guint8)(a / count) : 255;
      for (int py = by; py < by + bh; py++) {
        guchar *row = pixels + py * rowstride;
        for (int px = bx; px < bx + bw; px++) {
          guchar *p = row + px * channels;
          p[0] = ar;
          p[1] = ag;
          p[2] = ab;
          if (channels == 4)
            p[3] = aa;
        }
      }
    }
  }
}

static gboolean average_region_border(GdkPixbuf *pixbuf, int x, int y, int w,
                                      int h, guint8 fill[3]) {
  fill[0] = 245;
  fill[1] = 245;
  fill[2] = 245;

  guchar *pixels = NULL;
  int width = 0;
  int height = 0;
  int rowstride = 0;
  int channels = 0;
  if (!pixbuf_require_rgb8(pixbuf, &pixels, &width, &height, &rowstride,
                           &channels))
    return FALSE;

  int left = MAX(0, x - 1);
  int top = MAX(0, y - 1);
  int right = MIN(width - 1, x + w);
  int bottom = MIN(height - 1, y + h);
  guint64 r = 0;
  guint64 g = 0;
  guint64 b = 0;
  int count = 0;
  for (int py = top; py <= bottom; py++) {
    guchar *row = pixels + py * rowstride;
    for (int px = left; px <= right; px++) {
      if (px >= x && px < x + w && py >= y && py < y + h)
        continue;
      guchar *p = row + px * channels;
      r += p[0];
      g += p[1];
      b += p[2];
      count++;
    }
  }
  if (count == 0)
    return FALSE;
  fill[0] = (guint8)(r / count);
  fill[1] = (guint8)(g / count);
  fill[2] = (guint8)(b / count);
  return TRUE;
}

static int erase_color_bucket_index(guchar r, guchar g, guchar b) {
  int rb = r >> 4;
  int gb = g >> 4;
  int bb = b >> 4;
  return (rb << 8) | (gb << 4) | bb;
}

/* Erase prefers the dominant quantized border color so flat UI backgrounds do
 * not get muddied by antialiased text, shadows, or mixed edge samples. The
 * chosen bucket is averaged to preserve the local shade instead of snapping to
 * the bucket center.
 */
static gboolean dominant_region_border_color(GdkPixbuf *pixbuf, int x, int y,
                                             int w, int h, guint8 fill[3]) {
  fill[0] = 245;
  fill[1] = 245;
  fill[2] = 245;

  guchar *pixels = NULL;
  int width = 0;
  int height = 0;
  int rowstride = 0;
  int channels = 0;
  if (!pixbuf_require_rgb8(pixbuf, &pixels, &width, &height, &rowstride,
                           &channels))
    return FALSE;

  guint32 *counts = g_new0(guint32, SHAULA_ERASE_COLOR_BUCKET_COUNT);
  guint64 *sum_r = g_new0(guint64, SHAULA_ERASE_COLOR_BUCKET_COUNT);
  guint64 *sum_g = g_new0(guint64, SHAULA_ERASE_COLOR_BUCKET_COUNT);
  guint64 *sum_b = g_new0(guint64, SHAULA_ERASE_COLOR_BUCKET_COUNT);

  int left = MAX(0, x - 1);
  int top = MAX(0, y - 1);
  int right = MIN(width - 1, x + w);
  int bottom = MIN(height - 1, y + h);
  int best_bucket = -1;
  guint32 best_count = 0;

  for (int py = top; py <= bottom; py++) {
    guchar *row = pixels + py * rowstride;
    for (int px = left; px <= right; px++) {
      if (px >= x && px < x + w && py >= y && py < y + h)
        continue;
      guchar *p = row + px * channels;
      int bucket = erase_color_bucket_index(p[0], p[1], p[2]);
      counts[bucket]++;
      sum_r[bucket] += p[0];
      sum_g[bucket] += p[1];
      sum_b[bucket] += p[2];
      if (counts[bucket] > best_count) {
        best_count = counts[bucket];
        best_bucket = bucket;
      }
    }
  }

  gboolean found = best_bucket >= 0 && best_count > 0;
  if (found) {
    fill[0] = (guint8)(sum_r[best_bucket] / best_count);
    fill[1] = (guint8)(sum_g[best_bucket] / best_count);
    fill[2] = (guint8)(sum_b[best_bucket] / best_count);
  }

  g_free(counts);
  g_free(sum_r);
  g_free(sum_g);
  g_free(sum_b);
  return found;
}

static void erase_region(GdkPixbuf *pixbuf, int x, int y, int w, int h) {
  guchar *pixels = NULL;
  int width = 0;
  int height = 0;
  int rowstride = 0;
  int channels = 0;
  if (!pixbuf_require_rgb8(pixbuf, &pixels, &width, &height, &rowstride,
                           &channels))
    return;

  guint8 fill[3];
  if (!dominant_region_border_color(pixbuf, x, y, w, h, fill))
    average_region_border(pixbuf, x, y, w, h, fill);
  for (int py = y; py < y + h; py++) {
    guchar *row = pixels + py * rowstride;
    for (int px = x; px < x + w; px++) {
      guchar *p = row + px * channels;
      p[0] = fill[0];
      p[1] = fill[1];
      p[2] = fill[2];
      if (channels == 4)
        p[3] = 255;
    }
  }
}

/* Commits region pixel edits as one document edit after the output pixbuf is
 * ready. The temporary region selection is view/UI state, so it remains active
 * for repeat actions and is still excluded from history snapshots.
 */
static gboolean apply_region_edit(ShaulaPreviewState *state,
                                  ShaulaRegionEdit edit) {
  if (state == NULL || !state->has_region_selection)
    return FALSE;

  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  if (!crop_rect_to_pixels(state, state->region_selection_rect, &x, &y, &w, &h))
    return FALSE;

  GdkPixbuf *copy = gdk_pixbuf_copy(state->document.image);
  if (copy == NULL)
    return FALSE;

  switch (edit) {
  case SHAULA_REGION_EDIT_BLUR:
    pixelate_region(copy, x, y, w, h);
    break;
  case SHAULA_REGION_EDIT_ERASE:
    erase_region(copy, x, y, w, h);
    break;
  }

  shaula_preview_document_begin_edit(state);
  shaula_preview_document_replace_image(state, copy);
  shaula_preview_document_finish_edit(
      state, (ShaulaPreviewDocumentFinish){.queue_draw = TRUE});
  return TRUE;
}

static void remap_annotations_after_crop(ShaulaPreviewState *state,
                                         ShaulaRect crop,
                                         ShaulaAnnotation *remove_annotation) {
  if (state == NULL || state->document.annotations == NULL)
    return;

  shaula_annotation_editor_clear_selection(state);
  for (gint i = (gint)state->document.annotations->len - 1; i >= 0; i--) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->document.annotations, (guint)i);
    if (annotation == remove_annotation || annotation == NULL ||
        !shaula_annotation_apply_document_crop(annotation, crop)) {
      g_ptr_array_remove_index(state->document.annotations, (guint)i);
      continue;
    }
  }
}

static void remap_spotlight_regions_after_crop(ShaulaPreviewState *state,
                                               ShaulaRect crop) {
  if (state == NULL || state->document.spotlight_regions == NULL)
    return;

  for (gint i = (gint)state->document.spotlight_regions->len - 1; i >= 0; i--) {
    ShaulaSpotlightRegion *region = &g_array_index(
        state->document.spotlight_regions, ShaulaSpotlightRegion, (guint)i);
    ShaulaRect clamped = shaula_rect_clamped_c(
        (ShaulaRect){region->rect.x - crop.x, region->rect.y - crop.y,
                     region->rect.width, region->rect.height},
        crop.width, crop.height);
    if (shaula_rect_is_empty(clamped)) {
      g_array_remove_index(state->document.spotlight_regions, (guint)i);
      continue;
    }
    region->rect = clamped;
  }
}

/* Commits a crop as one document edit.
 *
 * The snapshot is pushed only after a valid output pixbuf exists, so failed or
 * tiny crops leave undo/redo untouched. Annotations outside the crop are
 * discarded and remaining annotations are translated into the new image
 * coordinate space.
 */
static gboolean apply_crop_to_rect(ShaulaPreviewState *state, ShaulaRect rect,
                                   ShaulaAnnotation *remove_annotation) {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  if (!crop_rect_to_pixels(state, rect, &x, &y, &w, &h))
    return FALSE;

  GdkPixbuf *sub = gdk_pixbuf_new_subpixbuf(state->document.image, x, y, w, h);
  if (sub == NULL)
    return FALSE;
  GdkPixbuf *copy = gdk_pixbuf_copy(sub);
  g_object_unref(sub);
  if (copy == NULL)
    return FALSE;

  shaula_preview_document_begin_edit(state);
  shaula_preview_document_replace_image(state, copy);
  remap_annotations_after_crop(state, (ShaulaRect){x, y, w, h},
                               remove_annotation);
  remap_spotlight_regions_after_crop(state, (ShaulaRect){x, y, w, h});
  shaula_preview_document_finish_edit(
      state, (ShaulaPreviewDocumentFinish){.clear_crop_draft = TRUE,
                                           .clear_region_selection = TRUE,
                                           .reset_tool_to_select = TRUE,
                                           .update_dimensions = TRUE,
                                           .fit_to_screen = TRUE});
  return TRUE;
}

gboolean shaula_preview_apply_crop_to_rect(ShaulaPreviewState *state,
                                           ShaulaRect rect) {
  return apply_crop_to_rect(state, rect, NULL);
}

gboolean shaula_preview_apply_crop(ShaulaPreviewState *state) {
  if (state == NULL || !state->has_crop_draft)
    return FALSE;
  return shaula_preview_apply_crop_to_rect(state, state->crop_draft);
}

gboolean shaula_preview_apply_crop_to_selected_rect(ShaulaPreviewState *state) {
  ShaulaAnnotation *annotation =
      shaula_annotation_editor_single_selection(state);
  if (annotation == NULL)
    return FALSE;

  switch (annotation->type) {
  case SHAULA_ANNOTATION_RECTANGLE:
    return apply_crop_to_rect(state, annotation->data.rectangle.rect,
                              annotation);
  case SHAULA_ANNOTATION_HIGHLIGHT:
  case SHAULA_ANNOTATION_ARROW:
  case SHAULA_ANNOTATION_TEXT:
  case SHAULA_ANNOTATION_MEASURE:
  case SHAULA_ANNOTATION_PEN:
  case SHAULA_ANNOTATION_IMAGE:
    return FALSE;
  }
  return FALSE;
}

gboolean
shaula_preview_apply_crop_to_region_selection(ShaulaPreviewState *state) {
  if (state == NULL || !state->has_region_selection)
    return FALSE;
  ShaulaRect rect = state->region_selection_rect;
  gboolean applied = apply_crop_to_rect(state, rect, NULL);
  if (applied)
    state->has_region_selection = FALSE;
  return applied;
}

gboolean shaula_preview_blur_region_selection(ShaulaPreviewState *state) {
  return apply_region_edit(state, SHAULA_REGION_EDIT_BLUR);
}

gboolean shaula_preview_erase_region_selection(ShaulaPreviewState *state) {
  return apply_region_edit(state, SHAULA_REGION_EDIT_ERASE);
}

gboolean shaula_preview_spotlight_rect(ShaulaPreviewState *state,
                                       ShaulaRect rect) {
  if (state == NULL)
    return FALSE;

  ShaulaRect spotlight =
      shaula_rect_clamped_c(rect, shaula_preview_image_width(state),
                            shaula_preview_image_height(state));
  if (spotlight.width < SHAULA_CROP_MIN_SIZE_PX ||
      spotlight.height < SHAULA_CROP_MIN_SIZE_PX)
    return FALSE;

  const char *debug = g_getenv("SHAULA_DEBUG_SPOTLIGHT");
  if (debug != NULL && debug[0] != '\0' && g_strcmp0(debug, "0") != 0) {
    g_printerr("[DEBUG-spotlight] persist input=(%.2f,%.2f %.2fx%.2f) "
               "stored=(%.2f,%.2f %.2fx%.2f) count_before=%u\n",
               rect.x, rect.y, rect.width, rect.height, spotlight.x,
               spotlight.y, spotlight.width, spotlight.height,
               state->document.spotlight_regions != NULL ? state->document.spotlight_regions->len
                                                : 0);
  }

  ShaulaSpotlightRegion region = {
      .rect = spotlight,
      .shape = state->tool_defaults.spotlight.shape,
      .border_color = state->tool_defaults.spotlight.border_color,
      .border_width = MAX(0.0, state->tool_defaults.spotlight.border_width),
  };
  shaula_preview_document_begin_edit(state);
  g_array_append_val(state->document.spotlight_regions, region);
  int spotlight_index = (int)state->document.spotlight_regions->len - 1;
  shaula_properties_hud_target_spotlight(&state->properties_hud,
                                         spotlight_index);
  if (debug != NULL && debug[0] != '\0' && g_strcmp0(debug, "0") != 0)
    g_printerr("[DEBUG-spotlight] persisted index=%d count_after=%u\n",
               spotlight_index, state->document.spotlight_regions->len);
  shaula_preview_document_finish_edit(
      state, (ShaulaPreviewDocumentFinish){.queue_draw = TRUE});
  return TRUE;
}

gboolean shaula_preview_spotlight_region_selection(ShaulaPreviewState *state) {
  if (state == NULL || !state->has_region_selection)
    return FALSE;
  return shaula_preview_spotlight_rect(state, state->region_selection_rect);
}
