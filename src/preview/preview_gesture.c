#include "preview_gesture.h"

#include "preview_annotation_behavior.h"

#include <math.h>
#include <string.h>

#include "preview_annotation_editor.h"
#include "preview_state.h"
#include "preview_toolbar.h"

#define SHAULA_SELECTION_EDGE_TARGET_PX 8.0
#define SHAULA_SELECTION_HANDLE_TARGET_PX 8.0
#define SHAULA_IMAGE_RESIZE_MIN_SCREEN_PX 16.0

static ShaulaPoint screen_to_image(const ShaulaPreviewState *state, double x,
                                   double y) {
  if (state == NULL || state->zoom <= 0.0)
    return (ShaulaPoint){0.0, 0.0};
  return (ShaulaPoint){(x - state->pan_x) / state->zoom,
                       (y - state->pan_y) / state->zoom};
}

static ShaulaPoint image_to_screen(const ShaulaPreviewState *state,
                                   ShaulaPoint point) {
  return (ShaulaPoint){state->pan_x + point.x * state->zoom,
                       state->pan_y + point.y * state->zoom};
}

static gboolean image_point_is_inside(ShaulaPreviewState *state,
                                      ShaulaPoint point) {
  return state != NULL && point.x >= 0.0 && point.y >= 0.0 &&
         point.x <= shaula_preview_image_width(state) &&
         point.y <= shaula_preview_image_height(state);
}

static ShaulaPoint clamped_image_point(ShaulaPreviewState *state,
                                       ShaulaPoint point) {
  return shaula_point_clamped(point, shaula_preview_image_width(state),
                              shaula_preview_image_height(state));
}

static ShaulaPoint arrow_bend_handle_point(const ShaulaAnnotation *arrow) {
  ShaulaPoint start = arrow->data.arrow.start;
  ShaulaPoint end = arrow->data.arrow.end;
  ShaulaPoint control =
      arrow->data.arrow.is_curved
          ? arrow->data.arrow.control
          : (ShaulaPoint){(start.x + end.x) / 2.0,
                          (start.y + end.y) / 2.0};
  return (ShaulaPoint){0.25 * start.x + 0.5 * control.x + 0.25 * end.x,
                       0.25 * start.y + 0.5 * control.y + 0.25 * end.y};
}

static ShaulaAnnotationResizeHandle
selected_resize_handle_at(ShaulaPreviewState *state, ShaulaPoint image_point,
                          double tolerance) {
  ShaulaAnnotation *annotation =
      shaula_annotation_editor_single_selection(state);
  if (annotation == NULL)
    return SHAULA_RESIZE_HANDLE_NONE;

  if (annotation->type == SHAULA_ANNOTATION_RECTANGLE ||
      annotation->type == SHAULA_ANNOTATION_IMAGE ||
      annotation->type == SHAULA_ANNOTATION_TEXT) {
    ShaulaRect rect = shaula_annotation_selection_bounds(annotation);
    const ShaulaPoint handles[] = {
        {rect.x, rect.y},
        {rect.x + rect.width / 2.0, rect.y},
        {rect.x + rect.width, rect.y},
        {rect.x + rect.width, rect.y + rect.height / 2.0},
        {rect.x + rect.width, rect.y + rect.height},
        {rect.x + rect.width / 2.0, rect.y + rect.height},
        {rect.x, rect.y + rect.height},
        {rect.x, rect.y + rect.height / 2.0},
    };
    const ShaulaAnnotationResizeHandle kinds[] = {
        SHAULA_RESIZE_HANDLE_NW, SHAULA_RESIZE_HANDLE_N,
        SHAULA_RESIZE_HANDLE_NE, SHAULA_RESIZE_HANDLE_E,
        SHAULA_RESIZE_HANDLE_SE, SHAULA_RESIZE_HANDLE_S,
        SHAULA_RESIZE_HANDLE_SW, SHAULA_RESIZE_HANDLE_W,
    };
    if (annotation->type == SHAULA_ANNOTATION_RECTANGLE) {
      for (guint i = 0; i < G_N_ELEMENTS(handles); i++) {
        if (shaula_point_distance(image_point, handles[i]) <= tolerance)
          return kinds[i];
      }
    } else {
      const guint corner_indices[] = {0, 2, 4, 6};
      for (guint i = 0; i < G_N_ELEMENTS(corner_indices); i++) {
        guint index = corner_indices[i];
        if (shaula_point_distance(image_point, handles[index]) <= tolerance)
          return kinds[index];
      }
    }
  } else if (annotation->type == SHAULA_ANNOTATION_ARROW) {
    if (shaula_point_distance(image_point, annotation->data.arrow.start) <=
        tolerance)
      return SHAULA_RESIZE_HANDLE_ARROW_START;
    if (shaula_point_distance(image_point, annotation->data.arrow.end) <=
        tolerance)
      return SHAULA_RESIZE_HANDLE_ARROW_END;
    if (shaula_point_distance(image_point,
                              arrow_bend_handle_point(annotation)) <= tolerance)
      return SHAULA_RESIZE_HANDLE_ARROW_CONTROL;
  }
  return SHAULA_RESIZE_HANDLE_NONE;
}

/* Selection chrome is only interactive near its visible edges. Screen-space
 * comparison keeps the target stable across zoom levels and is shared by
 * press and hover routing.
 */
static gboolean selection_box_edge_at(ShaulaPreviewState *state,
                                      double screen_x, double screen_y) {
  if (state == NULL || state->zoom <= 0.0)
    return FALSE;

  guint selected_count = shaula_annotation_editor_selected_count(state);
  ShaulaRect frame;
  if (selected_count > 1) {
    if (!shaula_annotation_editor_selected_bounds(state, &frame))
      return FALSE;
    frame.x -= 2.0;
    frame.y -= 2.0;
    frame.width += 4.0;
    frame.height += 4.0;
  } else if (selected_count == 1) {
    ShaulaAnnotation *annotation =
        shaula_annotation_editor_single_selection(state);
    if (annotation == NULL)
      return FALSE;
    frame = shaula_annotation_selection_bounds(annotation);
    double padding = annotation->type == SHAULA_ANNOTATION_RECTANGLE
                         ? MAX(3.0 / state->zoom, 1.0)
                         : 2.0;
    frame.x -= padding;
    frame.y -= padding;
    frame.width += 2.0 * padding;
    frame.height += 2.0 * padding;
  } else {
    return FALSE;
  }

  frame = shaula_rect_normalized(frame);
  ShaulaPoint top_left =
      image_to_screen(state, (ShaulaPoint){frame.x, frame.y});
  ShaulaPoint bottom_right = image_to_screen(
      state, (ShaulaPoint){frame.x + frame.width, frame.y + frame.height});
  double left = MIN(top_left.x, bottom_right.x);
  double right = MAX(top_left.x, bottom_right.x);
  double top = MIN(top_left.y, bottom_right.y);
  double bottom = MAX(top_left.y, bottom_right.y);
  double tolerance = SHAULA_SELECTION_EDGE_TARGET_PX;
  gboolean within_x =
      screen_x >= left - tolerance && screen_x <= right + tolerance;
  gboolean within_y =
      screen_y >= top - tolerance && screen_y <= bottom + tolerance;
  return (within_x && (fabs(screen_y - top) <= tolerance ||
                       fabs(screen_y - bottom) <= tolerance)) ||
         (within_y && (fabs(screen_x - left) <= tolerance ||
                       fabs(screen_x - right) <= tolerance));
}

static const char *cursor_for_resize_handle(
    ShaulaAnnotationResizeHandle handle) {
  switch (handle) {
  case SHAULA_RESIZE_HANDLE_NW:
  case SHAULA_RESIZE_HANDLE_SE:
    return "nwse-resize";
  case SHAULA_RESIZE_HANDLE_NE:
  case SHAULA_RESIZE_HANDLE_SW:
    return "nesw-resize";
  case SHAULA_RESIZE_HANDLE_E:
  case SHAULA_RESIZE_HANDLE_W:
    return "ew-resize";
  case SHAULA_RESIZE_HANDLE_N:
  case SHAULA_RESIZE_HANDLE_S:
    return "ns-resize";
  case SHAULA_RESIZE_HANDLE_ARROW_START:
  case SHAULA_RESIZE_HANDLE_ARROW_END:
  case SHAULA_RESIZE_HANDLE_ARROW_CONTROL:
    return "grab";
  case SHAULA_RESIZE_HANDLE_NONE:
    return "default";
  }
  return "default";
}

static gboolean capture_box_corner_points(
    ShaulaPreviewGestureState *gesture, ShaulaRect bounds,
    ShaulaAnnotationResizeHandle handle) {
  bounds = shaula_rect_normalized(bounds);
  double left = bounds.x;
  double top = bounds.y;
  double right = bounds.x + bounds.width;
  double bottom = bounds.y + bounds.height;
  switch (handle) {
  case SHAULA_RESIZE_HANDLE_NW:
    gesture->resize_dragged_corner = (ShaulaPoint){left, top};
    gesture->resize_fixed_corner = (ShaulaPoint){right, bottom};
    return TRUE;
  case SHAULA_RESIZE_HANDLE_NE:
    gesture->resize_dragged_corner = (ShaulaPoint){right, top};
    gesture->resize_fixed_corner = (ShaulaPoint){left, bottom};
    return TRUE;
  case SHAULA_RESIZE_HANDLE_SE:
    gesture->resize_dragged_corner = (ShaulaPoint){right, bottom};
    gesture->resize_fixed_corner = (ShaulaPoint){left, top};
    return TRUE;
  case SHAULA_RESIZE_HANDLE_SW:
    gesture->resize_dragged_corner = (ShaulaPoint){left, bottom};
    gesture->resize_fixed_corner = (ShaulaPoint){right, top};
    return TRUE;
  case SHAULA_RESIZE_HANDLE_NONE:
  case SHAULA_RESIZE_HANDLE_N:
  case SHAULA_RESIZE_HANDLE_E:
  case SHAULA_RESIZE_HANDLE_S:
  case SHAULA_RESIZE_HANDLE_W:
  case SHAULA_RESIZE_HANDLE_ARROW_START:
  case SHAULA_RESIZE_HANDLE_ARROW_END:
  case SHAULA_RESIZE_HANDLE_ARROW_CONTROL:
    return FALSE;
  }
  return FALSE;
}

static void capture_resize_origin(ShaulaPreviewState *state,
                                  ShaulaAnnotationResizeHandle handle) {
  ShaulaPreviewGestureState *gesture = &state->gesture;
  gesture->resize_handle = handle;
  ShaulaAnnotation *annotation =
      shaula_annotation_editor_single_selection(state);
  if (annotation == NULL)
    return;
  if (annotation->type == SHAULA_ANNOTATION_RECTANGLE) {
    gesture->resize_origin_rect =
        shaula_rect_normalized(annotation->data.rectangle.rect);
  } else if (annotation->type == SHAULA_ANNOTATION_IMAGE) {
    gesture->resize_origin_rect =
        shaula_rect_normalized(annotation->data.image.rect);
    (void)capture_box_corner_points(gesture, gesture->resize_origin_rect,
                                    handle);
  } else if (annotation->type == SHAULA_ANNOTATION_TEXT) {
    gesture->resize_origin_rect =
        shaula_annotation_selection_bounds(annotation);
    gesture->resize_origin_text_position = annotation->data.text.position;
    gesture->resize_origin_text_font_size = annotation->data.text.font_size;
    (void)capture_box_corner_points(gesture, gesture->resize_origin_rect,
                                    handle);
  } else if (annotation->type == SHAULA_ANNOTATION_ARROW) {
    gesture->resize_origin_arrow_start = annotation->data.arrow.start;
    gesture->resize_origin_arrow_end = annotation->data.arrow.end;
    gesture->resize_origin_arrow_control = annotation->data.arrow.control;
    gesture->resize_origin_arrow_curved = annotation->data.arrow.is_curved;
  }
}

static void resize_selected_rectangle(ShaulaPreviewState *state,
                                      ShaulaPoint point) {
  ShaulaAnnotation *annotation =
      shaula_annotation_editor_single_selection(state);
  if (annotation == NULL || annotation->type != SHAULA_ANNOTATION_RECTANGLE)
    return;

  ShaulaPreviewGestureState *gesture = &state->gesture;
  ShaulaRect origin = gesture->resize_origin_rect;
  double left = origin.x;
  double top = origin.y;
  double right = origin.x + origin.width;
  double bottom = origin.y + origin.height;

  switch (gesture->resize_handle) {
  case SHAULA_RESIZE_HANDLE_NW:
    left = point.x;
    top = point.y;
    break;
  case SHAULA_RESIZE_HANDLE_N:
    top = point.y;
    break;
  case SHAULA_RESIZE_HANDLE_NE:
    right = point.x;
    top = point.y;
    break;
  case SHAULA_RESIZE_HANDLE_E:
    right = point.x;
    break;
  case SHAULA_RESIZE_HANDLE_SE:
    right = point.x;
    bottom = point.y;
    break;
  case SHAULA_RESIZE_HANDLE_S:
    bottom = point.y;
    break;
  case SHAULA_RESIZE_HANDLE_SW:
    left = point.x;
    bottom = point.y;
    break;
  case SHAULA_RESIZE_HANDLE_W:
    left = point.x;
    break;
  case SHAULA_RESIZE_HANDLE_NONE:
  case SHAULA_RESIZE_HANDLE_ARROW_START:
  case SHAULA_RESIZE_HANDLE_ARROW_END:
  case SHAULA_RESIZE_HANDLE_ARROW_CONTROL:
    return;
  }

  ShaulaRect next = shaula_rect_from_points((ShaulaPoint){left, top},
                                            (ShaulaPoint){right, bottom});
  next = shaula_rect_clamped_c(next, shaula_preview_image_width(state),
                               shaula_preview_image_height(state));
  if (next.width < 3.0 || next.height < 3.0)
    return;
  annotation->data.rectangle.rect = next;
  shaula_annotation_update_bounds(annotation);
  state->document.modified = TRUE;
}

static gboolean resize_selected_image(ShaulaPreviewState *state,
                                      ShaulaPoint point) {
  ShaulaAnnotation *annotation =
      shaula_annotation_editor_single_selection(state);
  if (annotation == NULL || annotation->type != SHAULA_ANNOTATION_IMAGE)
    return FALSE;

  ShaulaPreviewGestureState *gesture = &state->gesture;
  (void)shaula_annotation_resize_image_from_origin(
      annotation, gesture->resize_origin_rect, gesture->resize_fixed_corner,
      gesture->resize_dragged_corner, point,
      (ShaulaRect){0.0, 0.0, shaula_preview_image_width(state),
                   shaula_preview_image_height(state)},
      SHAULA_IMAGE_RESIZE_MIN_SCREEN_PX / state->zoom);
  ShaulaRect rect = shaula_rect_normalized(annotation->data.image.rect);
  ShaulaRect origin = gesture->resize_origin_rect;
  gboolean changed = fabs(rect.x - origin.x) > 0.0001 ||
                     fabs(rect.y - origin.y) > 0.0001 ||
                     fabs(rect.width - origin.width) > 0.0001 ||
                     fabs(rect.height - origin.height) > 0.0001;
  if (changed)
    state->document.modified = TRUE;
  return changed;
}

static gboolean resize_selected_text(ShaulaPreviewState *state,
                                     ShaulaPoint point) {
  ShaulaAnnotation *annotation =
      shaula_annotation_editor_single_selection(state);
  if (annotation == NULL || annotation->type != SHAULA_ANNOTATION_TEXT)
    return FALSE;

  ShaulaPreviewGestureState *gesture = &state->gesture;
  (void)shaula_annotation_resize_text_from_origin(
      annotation, gesture->resize_origin_text_position,
      gesture->resize_origin_text_font_size, gesture->resize_origin_rect,
      gesture->resize_fixed_corner, gesture->resize_dragged_corner, point,
      (ShaulaRect){0.0, 0.0, shaula_preview_image_width(state),
                   shaula_preview_image_height(state)});
  gboolean changed =
      fabs(annotation->data.text.position.x -
           gesture->resize_origin_text_position.x) > 0.0001 ||
      fabs(annotation->data.text.position.y -
           gesture->resize_origin_text_position.y) > 0.0001 ||
      fabs(annotation->data.text.font_size -
           gesture->resize_origin_text_font_size) > 0.0001;
  if (changed)
    state->document.modified = TRUE;
  return changed;
}

static void resize_selected_arrow(ShaulaPreviewState *state,
                                  ShaulaPoint point) {
  ShaulaAnnotation *annotation =
      shaula_annotation_editor_single_selection(state);
  if (annotation == NULL || annotation->type != SHAULA_ANNOTATION_ARROW)
    return;

  ShaulaPreviewGestureState *gesture = &state->gesture;
  ShaulaPoint start = gesture->resize_origin_arrow_start;
  ShaulaPoint end = gesture->resize_origin_arrow_end;
  ShaulaPoint control = gesture->resize_origin_arrow_control;
  gboolean is_curved = gesture->resize_origin_arrow_curved;

  switch (gesture->resize_handle) {
  case SHAULA_RESIZE_HANDLE_ARROW_START:
    start = point;
    break;
  case SHAULA_RESIZE_HANDLE_ARROW_END:
    end = point;
    break;
  case SHAULA_RESIZE_HANDLE_ARROW_CONTROL:
    is_curved = TRUE;
    control.x = 2.0 * point.x - 0.5 * start.x - 0.5 * end.x;
    control.y = 2.0 * point.y - 0.5 * start.y - 0.5 * end.y;
    break;
  case SHAULA_RESIZE_HANDLE_NONE:
  case SHAULA_RESIZE_HANDLE_NW:
  case SHAULA_RESIZE_HANDLE_N:
  case SHAULA_RESIZE_HANDLE_NE:
  case SHAULA_RESIZE_HANDLE_E:
  case SHAULA_RESIZE_HANDLE_SE:
  case SHAULA_RESIZE_HANDLE_S:
  case SHAULA_RESIZE_HANDLE_SW:
  case SHAULA_RESIZE_HANDLE_W:
    return;
  }

  if (shaula_point_distance(start, end) < 3.0)
    return;
  annotation->data.arrow.start = start;
  annotation->data.arrow.end = end;
  annotation->data.arrow.control = control;
  annotation->data.arrow.is_curved = is_curved;
  shaula_annotation_update_bounds(annotation);
  state->document.modified = TRUE;
}

void shaula_preview_gesture_state_init(ShaulaPreviewGestureState *gesture) {
  shaula_preview_gesture_reset(gesture);
}

void shaula_preview_gesture_reset(ShaulaPreviewGestureState *gesture) {
  if (gesture == NULL)
    return;
  memset(gesture, 0, sizeof(*gesture));
  gesture->resize_handle = SHAULA_RESIZE_HANDLE_NONE;
}

void shaula_preview_gesture_begin_operation(ShaulaPreviewState *state,
                                            ShaulaPreviewOperation operation,
                                            ShaulaPoint point) {
  if (state == NULL)
    return;
  state->operation = operation;
  state->operation_changed = FALSE;
  state->drag_start_image = point;
  state->drag_current_image = point;
  state->drag_last_image = point;
  if (operation == SHAULA_OPERATION_CROP) {
    state->has_crop_draft = TRUE;
    state->crop_draft = (ShaulaRect){point.x, point.y, 0.0, 0.0};
  }
  if (operation == SHAULA_OPERATION_SELECT_REGION) {
    state->has_region_selection = FALSE;
    state->region_selection_rect =
        (ShaulaRect){point.x, point.y, 0.0, 0.0};
  }
  if ((operation == SHAULA_OPERATION_PEN ||
       operation == SHAULA_OPERATION_HIGHLIGHT) &&
      state->draft_pen_points != NULL) {
    g_array_set_size(state->draft_pen_points, 0);
    g_array_append_val(state->draft_pen_points, point);
  }
}

void shaula_preview_gesture_begin_pan(ShaulaPreviewState *state,
                                      double screen_x, double screen_y) {
  if (state == NULL)
    return;
  state->operation = SHAULA_OPERATION_PAN;
  state->drag_start_x = screen_x;
  state->drag_start_y = screen_y;
  state->pan_origin_x = state->pan_x;
  state->pan_origin_y = state->pan_y;
}

gboolean shaula_preview_gesture_begin_selection(
    ShaulaPreviewState *state, ShaulaPreviewPointerEvent event,
    const char **cursor_name_out) {
  if (cursor_name_out != NULL)
    *cursor_name_out = NULL;
  if (state == NULL || state->document.image == NULL || event.button != 1)
    return FALSE;

  shaula_preview_gesture_reset(&state->gesture);
  ShaulaPoint image_point =
      screen_to_image(state, event.screen_x, event.screen_y);
  gboolean inside = image_point_is_inside(state, image_point);
  ShaulaPoint clamped = clamped_image_point(state, image_point);
  double hit_tolerance = MAX(4.0, 8.0 / state->zoom);
  ShaulaAnnotationHit hit_result = {NULL, SHAULA_ANNOTATION_HIT_NONE};
  ShaulaAnnotationResizeHandle resize_handle =
      inside ? selected_resize_handle_at(
                   state, image_point,
                   SHAULA_SELECTION_HANDLE_TARGET_PX / state->zoom)
             : SHAULA_RESIZE_HANDLE_NONE;
  gboolean selection_edge_hit =
      resize_handle == SHAULA_RESIZE_HANDLE_NONE &&
      selection_box_edge_at(state, event.screen_x, event.screen_y);

  if (resize_handle != SHAULA_RESIZE_HANDLE_NONE) {
    hit_result = (ShaulaAnnotationHit){
        shaula_annotation_editor_single_selection(state),
        SHAULA_ANNOTATION_HIT_HANDLE};
  } else if (inside && (!selection_edge_hit || event.shift)) {
    hit_result = shaula_annotation_behavior_hit_test(
        state->document.annotations, image_point, hit_tolerance);
  }

  if (selection_edge_hit && !event.shift) {
    shaula_preview_clear_region_selection(state);
    shaula_preview_begin_history_gesture(state);
    shaula_preview_gesture_begin_operation(state, SHAULA_OPERATION_MOVE,
                                           image_point);
    if (cursor_name_out != NULL)
      *cursor_name_out = "grabbing";
    return TRUE;
  }

  if (selection_edge_hit && hit_result.annotation == NULL)
    return TRUE;

  ShaulaAnnotation *hit = hit_result.annotation;
  if (hit != NULL) {
    gboolean hit_was_selected =
        shaula_annotation_editor_is_selected(state, hit);
    shaula_preview_clear_region_selection(state);
    if (event.shift) {
      shaula_annotation_editor_toggle_selection(state, hit);
      return TRUE;
    }

    if (!hit_was_selected ||
        shaula_annotation_editor_selected_count(state) <= 1) {
      shaula_annotation_editor_select_only(state, hit);
    } else {
      state->gesture.pressed_annotation = hit;
      state->gesture.preserved_multi_selection = TRUE;
    }
    shaula_preview_begin_history_gesture(state);

    if (resize_handle != SHAULA_RESIZE_HANDLE_NONE) {
      capture_resize_origin(state, resize_handle);
      shaula_preview_gesture_begin_operation(
          state, SHAULA_OPERATION_RESIZE_ANNOTATION, image_point);
      if (cursor_name_out != NULL)
        *cursor_name_out = cursor_for_resize_handle(resize_handle);
      return TRUE;
    }

    shaula_preview_gesture_begin_operation(state, SHAULA_OPERATION_MOVE,
                                           image_point);
    if (cursor_name_out != NULL)
      *cursor_name_out = "grabbing";
    return TRUE;
  }

  if (inside) {
    shaula_annotation_editor_clear_selection(state);
    shaula_preview_clear_region_selection(state);
    shaula_preview_gesture_begin_operation(
        state, SHAULA_OPERATION_SELECT_REGION, clamped);
  } else {
    shaula_annotation_editor_clear_selection(state);
    shaula_preview_clear_region_selection(state);
    state->operation = SHAULA_OPERATION_NONE;
  }
  return TRUE;
}

gboolean shaula_preview_gesture_update(ShaulaPreviewState *state,
                                       double screen_x, double screen_y,
                                       double delta_x, double delta_y) {
  if (state == NULL)
    return FALSE;

  ShaulaPoint raw = screen_to_image(state, screen_x, screen_y);
  ShaulaPoint clamped = clamped_image_point(state, raw);
  switch (state->operation) {
  case SHAULA_OPERATION_PAN:
    state->fit_mode = FALSE;
    state->pan_x = state->pan_origin_x + delta_x;
    state->pan_y = state->pan_origin_y + delta_y;
    return TRUE;
  case SHAULA_OPERATION_MOVE: {
    double move_x = raw.x - state->drag_last_image.x;
    double move_y = raw.y - state->drag_last_image.y;
    if (!state->operation_changed &&
        (fabs(raw.x - state->drag_start_image.x) > 0.5 ||
         fabs(raw.y - state->drag_start_image.y) > 0.5))
      state->operation_changed = TRUE;
    if (shaula_annotation_editor_has_selection(state) &&
        state->operation_changed)
      shaula_annotation_editor_move_selected(state, move_x, move_y);
    state->drag_last_image = raw;
    return TRUE;
  }
  case SHAULA_OPERATION_BEND_ARROW: {
    if (!state->operation_changed &&
        (fabs(raw.x - state->drag_start_image.x) > 0.5 ||
         fabs(raw.y - state->drag_start_image.y) > 0.5))
      state->operation_changed = TRUE;
    ShaulaAnnotation *selected =
        shaula_annotation_editor_single_selection(state);
    if (selected != NULL && state->operation_changed &&
        selected->type == SHAULA_ANNOTATION_ARROW) {
      ShaulaPoint start = selected->data.arrow.start;
      ShaulaPoint end = selected->data.arrow.end;
      selected->data.arrow.is_curved = TRUE;
      selected->data.arrow.control.x =
          2.0 * raw.x - 0.5 * start.x - 0.5 * end.x;
      selected->data.arrow.control.y =
          2.0 * raw.y - 0.5 * start.y - 0.5 * end.y;
      shaula_annotation_update_bounds(selected);
      state->document.modified = TRUE;
    }
    state->drag_last_image = raw;
    return TRUE;
  }
  case SHAULA_OPERATION_RESIZE_ANNOTATION: {
    gboolean pointer_moved =
        fabs(clamped.x - state->drag_start_image.x) > 0.5 ||
        fabs(clamped.y - state->drag_start_image.y) > 0.5;
    ShaulaAnnotation *selected =
        shaula_annotation_editor_single_selection(state);
    if (selected != NULL &&
        (selected->type == SHAULA_ANNOTATION_IMAGE ||
         selected->type == SHAULA_ANNOTATION_TEXT)) {
      ShaulaPoint resize_point =
          pointer_moved ? clamped : state->gesture.resize_dragged_corner;
      state->operation_changed =
          selected->type == SHAULA_ANNOTATION_IMAGE
              ? resize_selected_image(state, resize_point)
              : resize_selected_text(state, resize_point);
    } else {
      if (!state->operation_changed && pointer_moved)
        state->operation_changed = TRUE;
      if (state->operation_changed && selected != NULL &&
          selected->type == SHAULA_ANNOTATION_RECTANGLE)
        resize_selected_rectangle(state, clamped);
      else if (state->operation_changed && selected != NULL &&
               selected->type == SHAULA_ANNOTATION_ARROW)
        resize_selected_arrow(state, clamped);
    }
    state->drag_last_image = clamped;
    return TRUE;
  }
  case SHAULA_OPERATION_SELECT_REGION: {
    state->drag_current_image = clamped;
    ShaulaRect rect =
        shaula_rect_from_points(state->drag_start_image, clamped);
    state->operation_changed = rect.width >= 3.0 && rect.height >= 3.0;
    state->region_selection_rect = rect;
    state->has_region_selection = state->operation_changed;
    return TRUE;
  }
  case SHAULA_OPERATION_SPOTLIGHT: {
    state->drag_current_image = clamped;
    ShaulaRect rect =
        shaula_rect_from_points(state->drag_start_image, clamped);
    state->operation_changed = rect.width >= 3.0 && rect.height >= 3.0;
    return TRUE;
  }
  case SHAULA_OPERATION_CROP:
    state->drag_current_image = clamped;
    state->crop_draft =
        shaula_rect_from_points(state->drag_start_image, clamped);
    return TRUE;
  case SHAULA_OPERATION_ARROW:
  case SHAULA_OPERATION_LINE:
  case SHAULA_OPERATION_RECTANGLE:
  case SHAULA_OPERATION_MEASURE:
    state->drag_current_image = clamped;
    return TRUE;
  case SHAULA_OPERATION_HIGHLIGHT:
  case SHAULA_OPERATION_PEN:
    if (state->draft_pen_points != NULL &&
        shaula_point_distance(state->drag_current_image, clamped) > 0.75) {
      g_array_append_val(state->draft_pen_points, clamped);
      state->drag_current_image = clamped;
    }
    return TRUE;
  case SHAULA_OPERATION_ERASE_ANNOTATIONS:
  case SHAULA_OPERATION_NONE:
  case SHAULA_OPERATION_TEXT:
    return FALSE;
  }
  return FALSE;
}

gboolean shaula_preview_gesture_end_selection(ShaulaPreviewState *state) {
  if (state == NULL ||
      !shaula_preview_gesture_is_selection_operation(state->operation))
    return FALSE;

  gboolean sync_text_hud = FALSE;
  if (state->operation == SHAULA_OPERATION_RESIZE_ANNOTATION &&
      state->operation_changed) {
    ShaulaAnnotation *selected =
        shaula_annotation_editor_single_selection(state);
    sync_text_hud =
        selected != NULL && selected->type == SHAULA_ANNOTATION_TEXT;
  }
  if (state->operation == SHAULA_OPERATION_MOVE ||
      state->operation == SHAULA_OPERATION_BEND_ARROW ||
      state->operation == SHAULA_OPERATION_RESIZE_ANNOTATION) {
    shaula_preview_commit_history_gesture(state, state->operation_changed);
  }
  if (sync_text_hud)
    shaula_properties_hud_sync_widgets(state);
  if (state->operation == SHAULA_OPERATION_MOVE &&
      !state->operation_changed &&
      state->gesture.preserved_multi_selection &&
      state->gesture.pressed_annotation != NULL) {
    shaula_annotation_editor_select_only(state,
                                         state->gesture.pressed_annotation);
  }
  if (state->operation == SHAULA_OPERATION_SELECT_REGION) {
    if (state->operation_changed) {
      ShaulaRect rect = shaula_rect_from_points(state->drag_start_image,
                                                state->drag_current_image);
      if (shaula_annotation_editor_select_intersecting_rect(state, rect) == 0)
        shaula_preview_toolbar_update_selection_state(state);
    } else {
      shaula_preview_clear_region_selection(state);
    }
  }

  state->operation = SHAULA_OPERATION_NONE;
  state->operation_changed = FALSE;
  shaula_preview_gesture_reset(&state->gesture);
  return TRUE;
}

const char *shaula_preview_gesture_hover_cursor(
    ShaulaPreviewState *state, double screen_x, double screen_y) {
  if (state == NULL || state->document.image == NULL || state->zoom <= 0.0)
    return "default";

  ShaulaPoint image_point = screen_to_image(state, screen_x, screen_y);
  gboolean inside = image_point_is_inside(state, image_point);
  ShaulaAnnotationResizeHandle resize_handle =
      inside ? selected_resize_handle_at(
                   state, image_point,
                   SHAULA_SELECTION_HANDLE_TARGET_PX / state->zoom)
             : SHAULA_RESIZE_HANDLE_NONE;
  if (resize_handle != SHAULA_RESIZE_HANDLE_NONE)
    return cursor_for_resize_handle(resize_handle);

  if (selection_box_edge_at(state, screen_x, screen_y))
    return "grab";

  if (inside) {
    ShaulaAnnotationHit hit = shaula_annotation_behavior_hit_test(
        state->document.annotations, image_point,
        MAX(4.0, 8.0 / state->zoom));
    if (hit.annotation != NULL)
      return "grab";
  }
  return "default";
}

gboolean shaula_preview_gesture_is_selection_operation(
    ShaulaPreviewOperation operation) {
  return operation == SHAULA_OPERATION_MOVE ||
         operation == SHAULA_OPERATION_BEND_ARROW ||
         operation == SHAULA_OPERATION_RESIZE_ANNOTATION ||
         operation == SHAULA_OPERATION_SELECT_REGION;
}
