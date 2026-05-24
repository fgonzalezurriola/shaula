#include "preview_state.h"

#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#include "preview_document_edit.h"
#include "preview_toolbar.h"

#define SHAULA_CROP_MIN_SIZE_PX 4
#define SHAULA_DUPLICATE_OFFSET_PX 12.0
#define SHAULA_PASTE_OFFSET_PX 12.0
#define SHAULA_ERASE_COLOR_BUCKETS_PER_CHANNEL 16
#define SHAULA_ERASE_COLOR_BUCKET_COUNT                                        \
  (SHAULA_ERASE_COLOR_BUCKETS_PER_CHANNEL *                                    \
   SHAULA_ERASE_COLOR_BUCKETS_PER_CHANNEL *                                    \
   SHAULA_ERASE_COLOR_BUCKETS_PER_CHANNEL)

static gboolean rect_fits_image(ShaulaRect rect, double image_width,
                                double image_height) {
  rect = shaula_rect_normalized(rect);
  return rect.x >= 0.0 && rect.y >= 0.0 && rect.x + rect.width <= image_width &&
         rect.y + rect.height <= image_height;
}

static gboolean path_has_prefix_dir(const char *path, const char *dir) {
  if (path == NULL || dir == NULL || dir[0] == '\0')
    return FALSE;
  gsize len = strlen(dir);
  return g_str_has_prefix(path, dir) &&
         (path[len] == '\0' || path[len] == G_DIR_SEPARATOR);
}

static gboolean preview_path_is_temporary_capture(const char *path) {
  /* Mirrors runtime/paths.zig capture artifact detection on the C helper side
   * until runtime-path checks can cross the C/Zig seam directly.
   */
  if (path == NULL)
    return FALSE;
  if (g_str_has_prefix(path, "/tmp/shaula/captures/"))
    return TRUE;
  const char *runtime_dir = g_getenv("XDG_RUNTIME_DIR");
  if (runtime_dir != NULL && runtime_dir[0] != '\0') {
    char *runtime_captures =
        g_build_filename(runtime_dir, "shaula", "captures", NULL);
    gboolean temporary = path_has_prefix_dir(path, runtime_captures);
    g_free(runtime_captures);
    if (temporary)
      return TRUE;
  }
  return FALSE;
}

static ShaulaAnnotation *
annotation_clone_with_offset(const ShaulaAnnotation *base, double dx,
                             double dy) {
  ShaulaAnnotation *clone = shaula_annotation_clone(base);
  if (clone == NULL)
    return NULL;
  clone->id = 0;
  clone->selected = FALSE;
  shaula_annotation_move(clone, dx, dy);
  return clone;
}

static void move_annotation_inside_image(ShaulaAnnotation *annotation,
                                         double image_width,
                                         double image_height) {
  if (annotation == NULL)
    return;
  ShaulaRect bounds = shaula_rect_normalized(annotation->bounds);
  double dx = 0.0;
  double dy = 0.0;

  if (bounds.width <= image_width) {
    if (bounds.x < 0.0)
      dx = -bounds.x;
    else if (bounds.x + bounds.width > image_width)
      dx = image_width - (bounds.x + bounds.width);
  } else {
    dx = -bounds.x;
  }

  if (bounds.height <= image_height) {
    if (bounds.y < 0.0)
      dy = -bounds.y;
    else if (bounds.y + bounds.height > image_height)
      dy = image_height - (bounds.y + bounds.height);
  } else {
    dy = -bounds.y;
  }

  if (dx != 0.0 || dy != 0.0)
    shaula_annotation_move(annotation, dx, dy);
}

static ShaulaAnnotation *
annotation_clone_for_paste(const ShaulaAnnotation *base, double image_width,
                           double image_height) {
  static const ShaulaPoint offsets[] = {
      {SHAULA_PASTE_OFFSET_PX, -SHAULA_PASTE_OFFSET_PX},
      {-SHAULA_PASTE_OFFSET_PX, -SHAULA_PASTE_OFFSET_PX},
      {SHAULA_PASTE_OFFSET_PX, SHAULA_PASTE_OFFSET_PX},
      {-SHAULA_PASTE_OFFSET_PX, SHAULA_PASTE_OFFSET_PX},
  };

  for (guint i = 0; i < G_N_ELEMENTS(offsets); i++) {
    ShaulaAnnotation *candidate =
        annotation_clone_with_offset(base, offsets[i].x, offsets[i].y);
    if (candidate == NULL)
      return NULL;
    if (rect_fits_image(candidate->bounds, image_width, image_height))
      return candidate;
    shaula_annotation_free(candidate);
  }

  ShaulaAnnotation *candidate = annotation_clone_with_offset(
      base, SHAULA_PASTE_OFFSET_PX, -SHAULA_PASTE_OFFSET_PX);
  if (candidate != NULL)
    move_annotation_inside_image(candidate, image_width, image_height);
  return candidate;
}

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
      preview_path_is_temporary_capture(path) ? g_strdup(path) : NULL;
  state->zoom = 1.0;
  state->fit_zoom = 1.0;
  state->fit_mode = TRUE;
  state->active_tool = SHAULA_TOOL_SELECT;
  state->previous_tool_before_space_pan = SHAULA_TOOL_SELECT;
  state->operation = SHAULA_OPERATION_NONE;
  state->last_action = "close";
  state->is_dark = TRUE;
  state->current_color = shaula_color_default();
  state->hover_color_valid = FALSE;
  state->hover_color = state->current_color;
  shaula_color_to_hex(state->hover_color, state->hover_hex);
  shaula_properties_hud_state_init(&state->properties_hud);
  state->draft_pen_points = g_array_new(FALSE, FALSE, sizeof(ShaulaPoint));
  state->selected_annotation_ids = g_array_new(FALSE, FALSE, sizeof(int));
  state->measure_tolerance = 32;
  state->toolbar_overflow_visible_count = -1;
}

void shaula_preview_state_free(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  if (state->draft_pen_points != NULL)
    g_array_unref(state->draft_pen_points);
  if (state->selected_annotation_ids != NULL)
    g_array_unref(state->selected_annotation_ids);
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
  if (state->color_swatch != NULL)
    gtk_widget_queue_draw(state->color_swatch);
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

static ShaulaAnnotation *annotation_by_id(ShaulaPreviewState *state, int id) {
  if (state == NULL || state->document.annotations == NULL || id <= 0)
    return NULL;
  for (guint i = 0; i < state->document.annotations->len; i++) {
    ShaulaAnnotation *annotation = g_ptr_array_index(state->document.annotations, i);
    if (annotation != NULL && annotation->id == id)
      return annotation;
  }
  return NULL;
}

static gboolean selected_ids_contain(ShaulaPreviewState *state, int id) {
  if (state == NULL || state->selected_annotation_ids == NULL || id <= 0)
    return FALSE;
  for (guint i = 0; i < state->selected_annotation_ids->len; i++) {
    if (g_array_index(state->selected_annotation_ids, int, i) == id)
      return TRUE;
  }
  return FALSE;
}

static void selected_ids_add(ShaulaPreviewState *state, int id) {
  if (state == NULL || state->selected_annotation_ids == NULL || id <= 0 ||
      selected_ids_contain(state, id))
    return;
  g_array_append_val(state->selected_annotation_ids, id);
}

static void selected_ids_remove(ShaulaPreviewState *state, int id) {
  if (state == NULL || state->selected_annotation_ids == NULL || id <= 0)
    return;
  for (guint i = 0; i < state->selected_annotation_ids->len; i++) {
    if (g_array_index(state->selected_annotation_ids, int, i) == id) {
      g_array_remove_index(state->selected_annotation_ids, i);
      return;
    }
  }
}

static void reset_property_targets(ShaulaPreviewState *state) {
  state->properties_hud.arrow_index = -1;
  state->properties_hud.spotlight_index = -1;
  state->properties_hud.rectangle_index = -1;
  state->properties_hud.measure_index = -1;
}

static void set_single_selection_properties(ShaulaPreviewState *state,
                                            ShaulaAnnotation *annotation) {
  reset_property_targets(state);
  state->selected_annotation = annotation;
  if (annotation == NULL) {
    state->properties_hud.active_panel = SHAULA_PROPERTIES_PANEL_NONE;
    return;
  }

  int annotation_index = -1;
  if (state->document.annotations != NULL) {
    for (guint i = 0; i < state->document.annotations->len; i++) {
      if (g_ptr_array_index(state->document.annotations, i) == annotation) {
        annotation_index = (int)i;
        break;
      }
    }
  }

  if (annotation->type == SHAULA_ANNOTATION_ARROW) {
    state->properties_hud.arrow_index = annotation_index;
    state->properties_hud.active_panel = SHAULA_PROPERTIES_PANEL_ARROW;
    state->properties_hud.arrow_color = annotation->color;
    state->properties_hud.arrow_stroke_width = annotation->stroke_width;
  } else if (annotation->type == SHAULA_ANNOTATION_RECTANGLE) {
    state->properties_hud.rectangle_index = annotation_index;
    state->properties_hud.active_panel = SHAULA_PROPERTIES_PANEL_RECTANGLE;
    state->properties_hud.rectangle_color = annotation->color;
    state->properties_hud.rectangle_stroke_width = annotation->stroke_width;
    state->properties_hud.rectangle_stroke_style =
        annotation->data.rectangle.stroke_style;
    state->properties_hud.rectangle_filled = annotation->data.rectangle.filled;
    state->properties_hud.rectangle_corners = annotation->data.rectangle.corners;
  } else if (annotation->type == SHAULA_ANNOTATION_PEN) {
    state->properties_hud.active_panel = SHAULA_PROPERTIES_PANEL_PEN;
    state->properties_hud.pen_color = annotation->color;
    state->properties_hud.pen_opacity = annotation->color.a;
    state->properties_hud.pen_stroke_width = annotation->stroke_width;
  } else if (annotation->type == SHAULA_ANNOTATION_HIGHLIGHT) {
    state->properties_hud.active_panel = SHAULA_PROPERTIES_PANEL_HIGHLIGHT;
    state->properties_hud.highlight_color = annotation->color;
    state->properties_hud.highlight_opacity = annotation->color.a;
    state->properties_hud.highlight_stroke_width = annotation->stroke_width;
  } else if (annotation->type == SHAULA_ANNOTATION_TEXT) {
    state->properties_hud.active_panel = SHAULA_PROPERTIES_PANEL_TEXT;
    state->properties_hud.text_color = annotation->color;
    state->properties_hud.text_font_size = annotation->data.text.font_size;
    state->properties_hud.text_align = annotation->data.text.align;
    state->properties_hud.text_font_mode = annotation->data.text.font_mode;
  } else if (annotation->type == SHAULA_ANNOTATION_MEASURE) {
    state->properties_hud.measure_index = annotation_index;
    state->properties_hud.active_panel = SHAULA_PROPERTIES_PANEL_MEASURE;
    state->properties_hud.measure_color = annotation->color;
    state->properties_hud.measure_stroke_width = annotation->stroke_width;
  } else {
    state->properties_hud.active_panel = SHAULA_PROPERTIES_PANEL_NONE;
  }
}

/* Selection IDs are the editor source of truth. Annotation flags are kept in
 * sync because rendering and document snapshots already persist that contract.
 */
static void sync_selection_from_ids(ShaulaPreviewState *state) {
  if (state == NULL || state->selected_annotation_ids == NULL)
    return;

  for (gint i = (gint)state->selected_annotation_ids->len - 1; i >= 0; i--) {
    int id = g_array_index(state->selected_annotation_ids, int, (guint)i);
    if (annotation_by_id(state, id) == NULL)
      g_array_remove_index(state->selected_annotation_ids, (guint)i);
  }

  ShaulaAnnotation *single = NULL;
  if (state->document.annotations != NULL) {
    for (guint i = 0; i < state->document.annotations->len; i++) {
      ShaulaAnnotation *item = g_ptr_array_index(state->document.annotations, i);
      item->selected = selected_ids_contain(state, item->id);
      if (item->selected)
        single = item;
    }
  }

  if (state->selected_annotation_ids->len == 1) {
    set_single_selection_properties(state, single);
  } else {
    reset_property_targets(state);
    state->selected_annotation = NULL;
    state->properties_hud.active_panel = SHAULA_PROPERTIES_PANEL_NONE;
  }
}

static void rebuild_selection_ids_from_flags(ShaulaPreviewState *state) {
  if (state == NULL || state->selected_annotation_ids == NULL)
    return;
  g_array_set_size(state->selected_annotation_ids, 0);
  if (state->document.annotations != NULL) {
    for (guint i = 0; i < state->document.annotations->len; i++) {
      ShaulaAnnotation *annotation = g_ptr_array_index(state->document.annotations, i);
      if (annotation != NULL && annotation->selected)
        selected_ids_add(state, annotation->id);
    }
  }
  sync_selection_from_ids(state);
}

void shaula_preview_select_annotation(ShaulaPreviewState *state,
                                      ShaulaAnnotation *annotation) {
  shaula_preview_select_only_annotation(state, annotation);
}

void shaula_preview_select_only_annotation(ShaulaPreviewState *state,
                                           ShaulaAnnotation *annotation) {
  if (state == NULL)
    return;
  gboolean changed = (annotation == NULL && shaula_preview_has_selection(state)) ||
                     (annotation != NULL &&
                      (state->selected_annotation_ids == NULL ||
                       state->selected_annotation_ids->len != 1 ||
                       !selected_ids_contain(state, annotation->id)));
  if (changed)
    shaula_preview_commit_history_gesture(state, TRUE);
  if (state->selected_annotation_ids != NULL)
    g_array_set_size(state->selected_annotation_ids, 0);
  if (annotation != NULL) {
    state->has_region_selection = FALSE;
    selected_ids_add(state, annotation->id);
  }
  sync_selection_from_ids(state);
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_selection_state(state);
}

void shaula_preview_toggle_annotation_selection(ShaulaPreviewState *state,
                                                ShaulaAnnotation *annotation) {
  if (state == NULL || annotation == NULL)
    return;
  shaula_preview_commit_history_gesture(state, TRUE);
  if (selected_ids_contain(state, annotation->id))
    selected_ids_remove(state, annotation->id);
  else
    selected_ids_add(state, annotation->id);
  if (shaula_preview_has_selection(state))
    state->has_region_selection = FALSE;
  sync_selection_from_ids(state);
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_selection_state(state);
}

void shaula_preview_add_annotation_to_selection(
    ShaulaPreviewState *state, ShaulaAnnotation *annotation) {
  if (state == NULL || annotation == NULL ||
      selected_ids_contain(state, annotation->id))
    return;
  shaula_preview_commit_history_gesture(state, TRUE);
  selected_ids_add(state, annotation->id);
  state->has_region_selection = FALSE;
  sync_selection_from_ids(state);
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_selection_state(state);
}

void shaula_preview_remove_annotation_from_selection(
    ShaulaPreviewState *state, ShaulaAnnotation *annotation) {
  if (state == NULL || annotation == NULL ||
      !selected_ids_contain(state, annotation->id))
    return;
  shaula_preview_commit_history_gesture(state, TRUE);
  selected_ids_remove(state, annotation->id);
  sync_selection_from_ids(state);
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_selection_state(state);
}

void shaula_preview_clear_selection(ShaulaPreviewState *state) {
  shaula_preview_select_only_annotation(state, NULL);
}

gboolean shaula_preview_has_selection(ShaulaPreviewState *state) {
  return state != NULL && state->selected_annotation_ids != NULL &&
         state->selected_annotation_ids->len > 0;
}

gboolean shaula_preview_is_annotation_selected(
    ShaulaPreviewState *state, ShaulaAnnotation *annotation) {
  return annotation != NULL && selected_ids_contain(state, annotation->id);
}

guint shaula_preview_selected_count(ShaulaPreviewState *state) {
  return state != NULL && state->selected_annotation_ids != NULL
             ? state->selected_annotation_ids->len
             : 0;
}

void shaula_preview_select_all_annotations(ShaulaPreviewState *state) {
  if (state == NULL || state->document.annotations == NULL ||
      state->document.annotations->len == 0)
    return;
  shaula_preview_commit_history_gesture(state, TRUE);
  g_array_set_size(state->selected_annotation_ids, 0);
  for (guint i = 0; i < state->document.annotations->len; i++) {
    ShaulaAnnotation *annotation = g_ptr_array_index(state->document.annotations, i);
    if (annotation != NULL)
      selected_ids_add(state, annotation->id);
  }
  state->has_region_selection = FALSE;
  sync_selection_from_ids(state);
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_selection_state(state);
}

guint shaula_preview_select_annotations_intersecting_rect(
    ShaulaPreviewState *state, ShaulaRect rect) {
  if (state == NULL || state->document.annotations == NULL ||
      state->document.annotations->len == 0)
    return 0;

  rect = shaula_rect_normalized(rect);
  if (shaula_rect_is_empty(rect))
    return 0;

  GArray *matched_ids = g_array_new(FALSE, FALSE, sizeof(int));
  for (guint i = 0; i < state->document.annotations->len; i++) {
    ShaulaAnnotation *annotation = g_ptr_array_index(state->document.annotations, i);
    if (annotation != NULL && shaula_rect_intersects(annotation->bounds, rect)) {
      int id = annotation->id;
      g_array_append_val(matched_ids, id);
    }
  }
  guint matched_count = matched_ids->len;
  if (matched_count == 0) {
    g_array_unref(matched_ids);
    return 0;
  }

  shaula_preview_commit_history_gesture(state, TRUE);
  g_array_set_size(state->selected_annotation_ids, 0);
  for (guint i = 0; i < matched_ids->len; i++) {
    int id = g_array_index(matched_ids, int, i);
    selected_ids_add(state, id);
  }
  g_array_unref(matched_ids);
  sync_selection_from_ids(state);
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_selection_state(state);
  return matched_count;
}

void shaula_preview_clear_region_selection(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  state->has_region_selection = FALSE;
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_selection_state(state);
}

static void add_annotation_without_history(ShaulaPreviewState *state,
                                           ShaulaAnnotation *annotation) {
  if (state == NULL || annotation == NULL)
    return;
  shaula_preview_document_add_annotation(&state->document, annotation);
  shaula_preview_select_annotation(state, annotation);
  shaula_preview_toolbar_update_history_state(state);
}

void shaula_preview_add_annotation(ShaulaPreviewState *state,
                                   ShaulaAnnotation *annotation) {
  if (state == NULL || annotation == NULL)
    return;
  shaula_preview_push_undo(state);
  add_annotation_without_history(state, annotation);
}

gboolean shaula_preview_can_duplicate_selected(ShaulaPreviewState *state) {
  return state != NULL && shaula_preview_selected_count(state) == 1 &&
         state->selected_annotation != NULL;
}

gboolean shaula_preview_can_delete_selected(ShaulaPreviewState *state) {
  return state != NULL && shaula_preview_has_selection(state);
}

gboolean
shaula_preview_can_copy_selected_annotation(ShaulaPreviewState *state) {
  return state != NULL && shaula_preview_selected_count(state) == 1 &&
         state->selected_annotation != NULL;
}

gboolean shaula_preview_copy_selected_annotation(ShaulaPreviewState *state) {
  if (!shaula_preview_can_copy_selected_annotation(state))
    return FALSE;

  ShaulaAnnotation *copy = shaula_annotation_clone(state->selected_annotation);
  if (copy == NULL)
    return FALSE;

  shaula_preview_annotation_clipboard_clear(
      &state->document.annotation_clipboard);
  copy->selected = FALSE;
  g_ptr_array_add(state->document.annotation_clipboard.annotations, copy);
  return TRUE;
}

gboolean shaula_preview_can_paste_annotation(ShaulaPreviewState *state) {
  return state != NULL && state->document.image != NULL &&
         state->document.annotation_clipboard.annotations != NULL &&
         state->document.annotation_clipboard.annotations->len > 0;
}

gboolean shaula_preview_duplicate_selected(ShaulaPreviewState *state) {
  if (!shaula_preview_can_duplicate_selected(state))
    return FALSE;

  ShaulaAnnotation *duplicate =
      shaula_annotation_clone(state->selected_annotation);
  if (duplicate == NULL)
    return FALSE;

  shaula_preview_push_undo(state);
  duplicate->selected = FALSE;
  shaula_annotation_move(duplicate, SHAULA_DUPLICATE_OFFSET_PX,
                         SHAULA_DUPLICATE_OFFSET_PX);
  shaula_preview_document_add_annotation(&state->document, duplicate);
  shaula_preview_select_annotation(state, duplicate);
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_history_state(state);
  shaula_preview_toolbar_update_selection_state(state);
  return TRUE;
}

gboolean shaula_preview_paste_annotation(ShaulaPreviewState *state) {
  if (!shaula_preview_can_paste_annotation(state))
    return FALSE;

  const ShaulaAnnotation *base = NULL;
  if (state->selected_annotation != NULL &&
      state->selected_annotation->id ==
          state->document.annotation_clipboard.last_pasted_id)
    base = state->selected_annotation;
  if (base == NULL)
    base = g_ptr_array_index(state->document.annotation_clipboard.annotations, 0);
  if (base == NULL)
    return FALSE;

  ShaulaAnnotation *pasted =
      annotation_clone_for_paste(base, shaula_preview_image_width(state),
                                 shaula_preview_image_height(state));
  if (pasted == NULL)
    return FALSE;

  shaula_preview_push_undo(state);
  pasted->selected = FALSE;
  shaula_preview_document_add_annotation(&state->document, pasted);
  shaula_preview_select_annotation(state, pasted);
  state->document.annotation_clipboard.last_pasted_id = pasted->id;
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_history_state(state);
  shaula_preview_toolbar_update_selection_state(state);
  return TRUE;
}

void shaula_preview_delete_selected(ShaulaPreviewState *state) {
  if (state == NULL || !shaula_preview_has_selection(state) ||
      state->document.annotations == NULL)
    return;

  shaula_preview_push_undo(state);
  state->has_region_selection = FALSE;
  for (gint i = (gint)state->document.annotations->len - 1; i >= 0; i--) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->document.annotations, (guint)i);
    if (shaula_preview_is_annotation_selected(state, annotation))
      shaula_preview_document_remove_annotation_at(&state->document, (guint)i);
  }
  g_array_set_size(state->selected_annotation_ids, 0);
  sync_selection_from_ids(state);
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_history_state(state);
  shaula_preview_toolbar_update_selection_state(state);
}

void shaula_preview_reset_annotations(ShaulaPreviewState *state) {
  if (state == NULL || state->document.annotations == NULL ||
      state->document.annotations->len == 0)
    return;

  shaula_preview_cancel_operation(state);

  /* Reset is a single document edit: capture the exact pre-clear state once,
   * and let the normal edit path clear redo so later annotation creation
   * replaces any undone reset branch.
   */
  shaula_preview_push_undo(state);
  state->selected_annotation = NULL;
  g_array_set_size(state->selected_annotation_ids, 0);
  shaula_preview_document_clear_annotations(&state->document);
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_history_state(state);
  shaula_preview_toolbar_update_selection_state(state);
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

void shaula_preview_replace_annotations(ShaulaPreviewState *state,
                                        GPtrArray *annotations) {
  if (state->document.annotations != NULL)
    g_ptr_array_unref(state->document.annotations);
  state->document.annotations = annotations;
  rebuild_selection_ids_from_flags(state);
  shaula_preview_toolbar_update_selection_state(state);
}

static void restore_snapshot(ShaulaPreviewState *state,
                             ShaulaPreviewSnapshot *snapshot) {
  shaula_preview_document_restore_snapshot(&state->document, snapshot,
                                           &state->selected_annotation);
  rebuild_selection_ids_from_flags(state);
  shaula_preview_toolbar_update_selection_state(state);
  state->has_crop_draft = FALSE;
  state->has_region_selection = FALSE;
  state->operation = SHAULA_OPERATION_NONE;
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
  state->operation = SHAULA_OPERATION_NONE;
  state->operation_changed = FALSE;
  state->drag_hit_annotation = NULL;
  state->drag_preserved_multi_selection = FALSE;
  state->active_resize_handle = SHAULA_RESIZE_HANDLE_NONE;
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

  state->selected_annotation = NULL;
  if (state->selected_annotation_ids != NULL)
    g_array_set_size(state->selected_annotation_ids, 0);
  for (gint i = (gint)state->document.annotations->len - 1; i >= 0; i--) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->document.annotations, (guint)i);
    if (annotation == remove_annotation || annotation == NULL ||
        !shaula_rect_intersects(annotation->bounds, crop)) {
      g_ptr_array_remove_index(state->document.annotations, (guint)i);
      continue;
    }
    annotation->selected = FALSE;
    shaula_annotation_move(annotation, -crop.x, -crop.y);
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
  if (state == NULL || shaula_preview_selected_count(state) != 1 ||
      state->selected_annotation == NULL)
    return FALSE;

  ShaulaAnnotation *annotation = state->selected_annotation;
  switch (annotation->type) {
  case SHAULA_ANNOTATION_RECTANGLE:
    return apply_crop_to_rect(state, annotation->data.rectangle.rect,
                              annotation);
  case SHAULA_ANNOTATION_HIGHLIGHT:
  case SHAULA_ANNOTATION_ARROW:
  case SHAULA_ANNOTATION_TEXT:
  case SHAULA_ANNOTATION_MEASURE:
  case SHAULA_ANNOTATION_PEN:
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
      .shape = state->properties_hud.spotlight_shape,
      .border_color = state->properties_hud.spotlight_border_color,
      .border_width = MAX(0.0, state->properties_hud.spotlight_border_width),
  };
  shaula_preview_document_begin_edit(state);
  g_array_append_val(state->document.spotlight_regions, region);
  state->properties_hud.spotlight_index = (int)state->document.spotlight_regions->len - 1;
  if (debug != NULL && debug[0] != '\0' && g_strcmp0(debug, "0") != 0)
    g_printerr("[DEBUG-spotlight] persisted index=%d count_after=%u\n",
               state->properties_hud.spotlight_index, state->document.spotlight_regions->len);
  state->properties_hud.active_panel = SHAULA_PROPERTIES_PANEL_SPOTLIGHT;
  shaula_preview_document_finish_edit(
      state, (ShaulaPreviewDocumentFinish){.queue_draw = TRUE});
  return TRUE;
}

gboolean shaula_preview_spotlight_region_selection(ShaulaPreviewState *state) {
  if (state == NULL || !state->has_region_selection)
    return FALSE;
  return shaula_preview_spotlight_rect(state, state->region_selection_rect);
}

void shaula_preview_set_properties_panel(ShaulaPreviewState *state,
                                         ShaulaPropertiesPanel panel) {
  if (state == NULL || state->properties_hud.active_panel == panel)
    return;
  if (panel == SHAULA_PROPERTIES_PANEL_NONE)
    shaula_preview_commit_history_gesture(state, TRUE);
  (void)shaula_properties_hud_set_panel(&state->properties_hud, panel);
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

static void begin_property_history_if_targeted(ShaulaPreviewState *state,
                                               gboolean has_target) {
  if (!has_target)
    return;
  gboolean starts_transaction = state->document.pending_history_snapshot == NULL;
  shaula_preview_begin_history_gesture(state);
  if (starts_transaction)
    shaula_preview_history_clear_redo(&state->document.history);
  shaula_preview_toolbar_update_history_state(state);
}

static gboolean colors_equal(ShaulaColor a, ShaulaColor b) {
  return fabs(a.r - b.r) <= 0.0001 && fabs(a.g - b.g) <= 0.0001 &&
         fabs(a.b - b.b) <= 0.0001 && fabs(a.a - b.a) <= 0.0001;
}

static ShaulaSpotlightRegion *
active_spotlight_region(ShaulaPreviewState *state) {
  if (state == NULL || state->document.spotlight_regions == NULL ||
      state->properties_hud.spotlight_index < 0 ||
      (guint)state->properties_hud.spotlight_index >= state->document.spotlight_regions->len)
    return NULL;
  return &g_array_index(state->document.spotlight_regions, ShaulaSpotlightRegion,
                        (guint)state->properties_hud.spotlight_index);
}

void shaula_preview_set_spotlight_border_color(ShaulaPreviewState *state,
                                               ShaulaColor color) {
  if (state == NULL)
    return;
  state->properties_hud.spotlight_border_color = color;
  ShaulaSpotlightRegion *region = active_spotlight_region(state);
  if (region != NULL && !colors_equal(region->border_color, color)) {
    begin_property_history_if_targeted(state, TRUE);
    region->border_color = color;
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_spotlight_border_width(ShaulaPreviewState *state,
                                               double width) {
  if (state == NULL)
    return;
  state->properties_hud.spotlight_border_width = CLAMP(width, 0.0, 16.0);
  ShaulaSpotlightRegion *region = active_spotlight_region(state);
  if (region != NULL &&
      fabs(region->border_width - state->properties_hud.spotlight_border_width) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    region->border_width = state->properties_hud.spotlight_border_width;
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_spotlight_shape(ShaulaPreviewState *state,
                                        ShaulaSpotlightShape shape) {
  if (state == NULL)
    return;
  state->properties_hud.spotlight_shape = shape;
  ShaulaSpotlightRegion *region = active_spotlight_region(state);
  if (region != NULL && region->shape != shape) {
    begin_property_history_if_targeted(state, TRUE);
    region->shape = shape;
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

/* Returns the annotation currently targeted by the Arrow properties HUD, or
 * NULL when no valid target exists.
 */
static ShaulaAnnotation *active_arrow_annotation(ShaulaPreviewState *state) {
  if (state == NULL || state->document.annotations == NULL ||
      state->properties_hud.arrow_index < 0 ||
      (guint)state->properties_hud.arrow_index >= state->document.annotations->len)
    return NULL;
  ShaulaAnnotation *annotation =
      g_ptr_array_index(state->document.annotations, (guint)state->properties_hud.arrow_index);
  if (annotation != NULL && annotation->type == SHAULA_ANNOTATION_ARROW)
    return annotation;
  return NULL;
}

static ShaulaAnnotation *
active_rectangle_annotation(ShaulaPreviewState *state) {
  if (state == NULL || state->document.annotations == NULL ||
      state->properties_hud.rectangle_index < 0 ||
      (guint)state->properties_hud.rectangle_index >= state->document.annotations->len)
    return NULL;
  ShaulaAnnotation *annotation = g_ptr_array_index(
      state->document.annotations, (guint)state->properties_hud.rectangle_index);
  if (annotation != NULL && annotation->type == SHAULA_ANNOTATION_RECTANGLE)
    return annotation;
  return NULL;
}

void shaula_preview_set_arrow_color(ShaulaPreviewState *state,
                                    ShaulaColor color) {
  if (state == NULL)
    return;
  state->properties_hud.arrow_color = color;
  ShaulaAnnotation *arrow = active_arrow_annotation(state);
  if (arrow != NULL && !colors_equal(arrow->color, color)) {
    begin_property_history_if_targeted(state, TRUE);
    arrow->color = color;
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_arrow_stroke_width(ShaulaPreviewState *state,
                                           double width) {
  if (state == NULL)
    return;
  state->properties_hud.arrow_stroke_width = CLAMP(width, 1.0, 12.0);
  ShaulaAnnotation *arrow = active_arrow_annotation(state);
  if (arrow != NULL &&
      fabs(arrow->stroke_width - state->properties_hud.arrow_stroke_width) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    arrow->stroke_width = state->properties_hud.arrow_stroke_width;
    shaula_annotation_update_bounds(arrow);
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_arrow_stroke_style(ShaulaPreviewState *state,
                                           PreviewArrowStrokeStyle style) {
  if (state == NULL)
    return;
  ShaulaAnnotation *arrow = active_arrow_annotation(state);
  if (arrow == NULL)
    return;
  if (style < PREVIEW_ARROW_STROKE_SOLID || style > PREVIEW_ARROW_STROKE_DOTTED)
    return;
  if (arrow->data.arrow.stroke_style == style)
    return;

  begin_property_history_if_targeted(state, TRUE);
  arrow->data.arrow.stroke_style = style;
  state->document.modified = TRUE;
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_rectangle_color(ShaulaPreviewState *state,
                                        ShaulaColor color) {
  if (state == NULL)
    return;
  state->properties_hud.rectangle_color = color;
  ShaulaAnnotation *rectangle = active_rectangle_annotation(state);
  if (rectangle != NULL && !colors_equal(rectangle->color, color)) {
    begin_property_history_if_targeted(state, TRUE);
    rectangle->color = color;
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_rectangle_stroke_width(ShaulaPreviewState *state,
                                               double width) {
  if (state == NULL)
    return;
  state->properties_hud.rectangle_stroke_width = CLAMP(width, 1.0, 12.0);
  ShaulaAnnotation *rectangle = active_rectangle_annotation(state);
  if (rectangle != NULL &&
      fabs(rectangle->stroke_width - state->properties_hud.rectangle_stroke_width) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    rectangle->stroke_width = state->properties_hud.rectangle_stroke_width;
    shaula_annotation_update_bounds(rectangle);
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_rectangle_stroke_style(ShaulaPreviewState *state,
                                               PreviewArrowStrokeStyle style) {
  if (state == NULL || style < PREVIEW_ARROW_STROKE_SOLID ||
      style > PREVIEW_ARROW_STROKE_DASHED)
    return;
  state->properties_hud.rectangle_stroke_style = style;
  ShaulaAnnotation *rectangle = active_rectangle_annotation(state);
  if (rectangle != NULL && rectangle->data.rectangle.stroke_style != style) {
    begin_property_history_if_targeted(state, TRUE);
    rectangle->data.rectangle.stroke_style = style;
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_rectangle_filled(ShaulaPreviewState *state,
                                         gboolean filled) {
  if (state == NULL)
    return;
  state->properties_hud.rectangle_filled = filled;
  ShaulaAnnotation *rectangle = active_rectangle_annotation(state);
  if (rectangle != NULL && rectangle->data.rectangle.filled != filled) {
    begin_property_history_if_targeted(state, TRUE);
    rectangle->data.rectangle.filled = filled;
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_rectangle_corners(ShaulaPreviewState *state,
                                          PreviewRectangleCorners corners) {
  if (state == NULL || corners < PREVIEW_RECTANGLE_CORNERS_ROUNDED ||
      corners > PREVIEW_RECTANGLE_CORNERS_SQUARE)
    return;
  state->properties_hud.rectangle_corners = corners;
  ShaulaAnnotation *rectangle = active_rectangle_annotation(state);
  if (rectangle != NULL && rectangle->data.rectangle.corners != corners) {
    begin_property_history_if_targeted(state, TRUE);
    rectangle->data.rectangle.corners = corners;
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

static ShaulaAnnotation *
selected_annotation_of_type(ShaulaPreviewState *state,
                            ShaulaAnnotationType type) {
  if (state == NULL || shaula_preview_selected_count(state) != 1 ||
      state->selected_annotation == NULL ||
      state->selected_annotation->type != type)
    return NULL;
  return state->selected_annotation;
}

void shaula_preview_set_pen_color(ShaulaPreviewState *state,
                                  ShaulaColor color) {
  if (state == NULL)
    return;
  color.a = CLAMP(state->properties_hud.pen_opacity, 0.1, 1.0);
  state->properties_hud.pen_color = color;
  ShaulaAnnotation *pen =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_PEN);
  if (pen != NULL && !colors_equal(pen->color, color)) {
    begin_property_history_if_targeted(state, TRUE);
    pen->color = color;
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_pen_stroke_width(ShaulaPreviewState *state,
                                         double width) {
  if (state == NULL)
    return;
  state->properties_hud.pen_stroke_width = CLAMP(width, 1.0, 24.0);
  ShaulaAnnotation *pen =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_PEN);
  if (pen != NULL &&
      fabs(pen->stroke_width - state->properties_hud.pen_stroke_width) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    pen->stroke_width = state->properties_hud.pen_stroke_width;
    shaula_annotation_update_bounds(pen);
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_pen_opacity(ShaulaPreviewState *state, double opacity) {
  if (state == NULL)
    return;
  state->properties_hud.pen_opacity = CLAMP(opacity, 0.1, 1.0);
  state->properties_hud.pen_color.a = state->properties_hud.pen_opacity;
  ShaulaAnnotation *pen =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_PEN);
  if (pen != NULL && fabs(pen->color.a - state->properties_hud.pen_opacity) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    pen->color.a = state->properties_hud.pen_opacity;
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_highlight_color(ShaulaPreviewState *state,
                                        ShaulaColor color) {
  if (state == NULL)
    return;
  color.a = CLAMP(state->properties_hud.highlight_opacity, 0.05, 1.0);
  state->properties_hud.highlight_color = color;
  ShaulaAnnotation *highlight =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_HIGHLIGHT);
  if (highlight != NULL && !colors_equal(highlight->color, color)) {
    begin_property_history_if_targeted(state, TRUE);
    highlight->color = color;
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_highlight_stroke_width(ShaulaPreviewState *state,
                                               double width) {
  if (state == NULL)
    return;
  state->properties_hud.highlight_stroke_width = CLAMP(width, 4.0, 48.0);
  ShaulaAnnotation *highlight =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_HIGHLIGHT);
  if (highlight != NULL &&
      fabs(highlight->stroke_width - state->properties_hud.highlight_stroke_width) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    highlight->stroke_width = state->properties_hud.highlight_stroke_width;
    shaula_annotation_update_bounds(highlight);
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_highlight_opacity(ShaulaPreviewState *state,
                                          double opacity) {
  if (state == NULL)
    return;
  state->properties_hud.highlight_opacity = CLAMP(opacity, 0.05, 1.0);
  state->properties_hud.highlight_color.a = state->properties_hud.highlight_opacity;
  ShaulaAnnotation *highlight =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_HIGHLIGHT);
  if (highlight != NULL &&
      fabs(highlight->color.a - state->properties_hud.highlight_opacity) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    highlight->color.a = state->properties_hud.highlight_opacity;
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_text_color(ShaulaPreviewState *state,
                                   ShaulaColor color) {
  if (state == NULL)
    return;
  color.a = 1.0;
  state->properties_hud.text_color = color;
  ShaulaAnnotation *text = state->text_entry == NULL
                               ? selected_annotation_of_type(
                                     state, SHAULA_ANNOTATION_TEXT)
                               : NULL;
  if (text != NULL && !colors_equal(text->color, color)) {
    begin_property_history_if_targeted(state, TRUE);
    text->color = color;
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_text_font_size(ShaulaPreviewState *state,
                                       double font_size) {
  if (state == NULL)
    return;
  state->properties_hud.text_font_size = CLAMP(font_size, 12.0, 72.0);
  ShaulaAnnotation *text = state->text_entry == NULL
                               ? selected_annotation_of_type(
                                     state, SHAULA_ANNOTATION_TEXT)
                               : NULL;
  if (text != NULL &&
      fabs(text->data.text.font_size - state->properties_hud.text_font_size) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    text->data.text.font_size = state->properties_hud.text_font_size;
    shaula_annotation_update_bounds(text);
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_text_align(ShaulaPreviewState *state,
                                   ShaulaTextAlign align) {
  if (state == NULL)
    return;
  state->properties_hud.text_align = align;
  ShaulaAnnotation *text = state->text_entry == NULL
                               ? selected_annotation_of_type(
                                     state, SHAULA_ANNOTATION_TEXT)
                               : NULL;
  if (text != NULL && text->data.text.align != align) {
    begin_property_history_if_targeted(state, TRUE);
    text->data.text.align = align;
    shaula_annotation_update_bounds(text);
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_text_font_mode(ShaulaPreviewState *state,
                                       ShaulaTextFontMode font_mode) {
  if (state == NULL)
    return;
  if (font_mode != SHAULA_TEXT_FONT_SKETCH)
    font_mode = SHAULA_TEXT_FONT_NORMAL;
  state->properties_hud.text_font_mode = font_mode;
  ShaulaAnnotation *text = state->text_entry == NULL
                               ? selected_annotation_of_type(
                                     state, SHAULA_ANNOTATION_TEXT)
                               : NULL;
  if (text != NULL && text->data.text.font_mode != font_mode) {
    begin_property_history_if_targeted(state, TRUE);
    text->data.text.font_mode = font_mode;
    shaula_annotation_update_bounds(text);
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

static ShaulaAnnotation *active_measure_annotation(ShaulaPreviewState *state) {
  if (state == NULL || state->document.annotations == NULL ||
      state->properties_hud.measure_index < 0 ||
      (guint)state->properties_hud.measure_index >= state->document.annotations->len)
    return NULL;
  ShaulaAnnotation *annotation =
      g_ptr_array_index(state->document.annotations, (guint)state->properties_hud.measure_index);
  if (annotation != NULL && annotation->type == SHAULA_ANNOTATION_MEASURE)
    return annotation;
  return NULL;
}

void shaula_preview_set_measure_color(ShaulaPreviewState *state,
                                      ShaulaColor color) {
  if (state == NULL)
    return;
  state->properties_hud.measure_color = color;
  ShaulaAnnotation *measure = active_measure_annotation(state);
  if (measure != NULL && !colors_equal(measure->color, color)) {
    begin_property_history_if_targeted(state, TRUE);
    measure->color = color;
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_measure_stroke_width(ShaulaPreviewState *state,
                                             double width) {
  if (state == NULL)
    return;
  state->properties_hud.measure_stroke_width = CLAMP(width, 1.0, 8.0);
  ShaulaAnnotation *measure = active_measure_annotation(state);
  if (measure != NULL &&
      fabs(measure->stroke_width - state->properties_hud.measure_stroke_width) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    measure->stroke_width = state->properties_hud.measure_stroke_width;
    shaula_annotation_update_bounds(measure);
    state->document.modified = TRUE;
  }
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}
