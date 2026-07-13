#include "overlay_selection_session.h"

#include <stdlib.h>

enum {
  RESIZE_HIT_RADIUS = 10,
  MOVE_HIT_RADIUS = 24,
  CREATE_THRESHOLD = 6,
};

static gboolean point_in_selection(ShaulaRect selection, ShaulaPoint point) {
  return point.x >= selection.x && point.x <= selection.x + selection.width &&
         point.y >= selection.y && point.y <= selection.y + selection.height;
}

static gboolean point_near_selection_border(ShaulaRect selection,
                                            ShaulaPoint point, int radius) {
  if (!point_in_selection(selection, point))
    return FALSE;
  int left = selection.x;
  int top = selection.y;
  int right = selection.x + selection.width;
  int bottom = selection.y + selection.height;
  return abs(point.x - left) <= radius || abs(point.x - right) <= radius ||
         abs(point.y - top) <= radius || abs(point.y - bottom) <= radius;
}

static gboolean point_near(ShaulaPoint a, ShaulaPoint b, int radius) {
  return abs(a.x - b.x) <= radius && abs(a.y - b.y) <= radius;
}

static ShaulaResizeHandle resize_handle_at(ShaulaRect selection,
                                           ShaulaPoint point) {
  int left = selection.x;
  int top = selection.y;
  int right = selection.x + selection.width;
  int bottom = selection.y + selection.height;
  int mid_x = selection.x + selection.width / 2;
  int mid_y = selection.y + selection.height / 2;

  if (point_near(point, (ShaulaPoint){.x = left, .y = top},
                 RESIZE_HIT_RADIUS))
    return HANDLE_TOP_LEFT;
  if (point_near(point, (ShaulaPoint){.x = right, .y = top},
                 RESIZE_HIT_RADIUS))
    return HANDLE_TOP_RIGHT;
  if (point_near(point, (ShaulaPoint){.x = right, .y = bottom},
                 RESIZE_HIT_RADIUS))
    return HANDLE_BOTTOM_RIGHT;
  if (point_near(point, (ShaulaPoint){.x = left, .y = bottom},
                 RESIZE_HIT_RADIUS))
    return HANDLE_BOTTOM_LEFT;
  if (point_near(point, (ShaulaPoint){.x = mid_x, .y = top},
                 RESIZE_HIT_RADIUS))
    return HANDLE_TOP;
  if (point_near(point, (ShaulaPoint){.x = right, .y = mid_y},
                 RESIZE_HIT_RADIUS))
    return HANDLE_RIGHT;
  if (point_near(point, (ShaulaPoint){.x = mid_x, .y = bottom},
                 RESIZE_HIT_RADIUS))
    return HANDLE_BOTTOM;
  if (point_near(point, (ShaulaPoint){.x = left, .y = mid_y},
                 RESIZE_HIT_RADIUS))
    return HANDLE_LEFT;
  return HANDLE_NONE;
}

static ShaulaOverlayCursor cursor_for_handle(ShaulaResizeHandle handle) {
  switch (handle) {
  case HANDLE_LEFT:
  case HANDLE_RIGHT:
    return SHAULA_OVERLAY_CURSOR_RESIZE_EW;
  case HANDLE_TOP:
  case HANDLE_BOTTOM:
    return SHAULA_OVERLAY_CURSOR_RESIZE_NS;
  case HANDLE_TOP_LEFT:
  case HANDLE_BOTTOM_RIGHT:
    return SHAULA_OVERLAY_CURSOR_RESIZE_NWSE;
  case HANDLE_TOP_RIGHT:
  case HANDLE_BOTTOM_LEFT:
    return SHAULA_OVERLAY_CURSOR_RESIZE_NESW;
  case HANDLE_NONE:
  default:
    return SHAULA_OVERLAY_CURSOR_DEFAULT;
  }
}

static ShaulaOverlayCursor resolve_cursor(
    const ShaulaOverlaySelectionSession *session, ShaulaPoint point) {
  if (session->view.has_selection) {
    ShaulaResizeHandle handle = resize_handle_at(session->view.selection, point);
    if (handle != HANDLE_NONE)
      return cursor_for_handle(handle);
    if (point_near_selection_border(session->view.selection, point,
                                    MOVE_HIT_RADIUS))
      return SHAULA_OVERLAY_CURSOR_GRAB;
  }
  return SHAULA_OVERLAY_CURSOR_CROSSHAIR;
}

static void update_confirmable(ShaulaOverlaySelectionSession *session) {
  session->view.confirmable =
      session->view.has_selection && session->view.selection.width > 0 &&
      session->view.selection.height > 0;
}

void shaula_overlay_selection_session_init(
    ShaulaOverlaySelectionSession *session, ShaulaPoint bounds) {
  g_return_if_fail(session != NULL);
  *session = (ShaulaOverlaySelectionSession){
      .bounds = bounds,
      .view =
          {
              .drag_mode = SHAULA_OVERLAY_DRAG_NONE,
              .active_handle = HANDLE_NONE,
              .hover_handle = HANDLE_NONE,
              .cursor = SHAULA_OVERLAY_CURSOR_CROSSHAIR,
          },
  };
}

void shaula_overlay_selection_session_set_bounds(
    ShaulaOverlaySelectionSession *session, ShaulaPoint bounds) {
  g_return_if_fail(session != NULL);
  session->bounds = bounds;
  if (session->view.has_selection) {
    ShaulaRect adjusted;
    if (clamp_selection_preserve_size(session->view.selection, bounds,
                                      &adjusted))
      session->view.selection = adjusted;
    else
      session->view.has_selection = FALSE;
  }
  update_confirmable(session);
}

const ShaulaOverlaySelectionView *shaula_overlay_selection_session_view(
    const ShaulaOverlaySelectionSession *session) {
  return session != NULL ? &session->view : NULL;
}

gboolean shaula_overlay_selection_session_set_selection(
    ShaulaOverlaySelectionSession *session, ShaulaRect selection,
    gboolean preserve_size) {
  g_return_val_if_fail(session != NULL, FALSE);
  ShaulaRect adjusted;
  gboolean valid = preserve_size
                       ? clamp_selection_preserve_size(selection, session->bounds,
                                                       &adjusted)
                       : clamp_selection(selection, session->bounds, &adjusted);
  if (!valid)
    return FALSE;
  if (session->view.has_aspect) {
    ShaulaRect aspect_adjusted;
    valid = preserve_size
                ? apply_aspect_from_center_preserve(
                      adjusted, session->view.aspect, session->bounds,
                      &aspect_adjusted)
                : apply_aspect_from_center(adjusted, session->view.aspect,
                                           session->bounds, &aspect_adjusted);
    if (valid)
      adjusted = aspect_adjusted;
  }
  session->view.selection = adjusted;
  session->view.has_selection = TRUE;
  update_confirmable(session);
  return TRUE;
}

gboolean shaula_overlay_selection_session_set_aspect(
    ShaulaOverlaySelectionSession *session, gboolean enabled,
    ShaulaAspect aspect, gboolean preserve_size) {
  g_return_val_if_fail(session != NULL, FALSE);
  session->view.has_aspect = enabled && aspect.width > 0 && aspect.height > 0;
  session->view.aspect = session->view.has_aspect ? aspect : (ShaulaAspect){0};
  if (!session->view.has_aspect || !session->view.has_selection)
    return FALSE;

  ShaulaRect adjusted;
  gboolean valid = preserve_size
                       ? apply_aspect_from_center_preserve(
                             session->view.selection, session->view.aspect,
                             session->bounds, &adjusted)
                       : apply_aspect_from_center(
                             session->view.selection, session->view.aspect,
                             session->bounds, &adjusted);
  if (!valid)
    return FALSE;
  gboolean changed = adjusted.x != session->view.selection.x ||
                     adjusted.y != session->view.selection.y ||
                     adjusted.width != session->view.selection.width ||
                     adjusted.height != session->view.selection.height;
  session->view.selection = adjusted;
  update_confirmable(session);
  return changed;
}

void shaula_overlay_selection_session_begin(
    ShaulaOverlaySelectionSession *session, ShaulaPoint point,
    gboolean blocked) {
  g_return_if_fail(session != NULL);
  session->drag_start = point;
  session->drag_origin = session->view.selection;
  session->view.active_handle = HANDLE_NONE;
  session->view.hover_handle = HANDLE_NONE;
  if (blocked) {
    session->view.drag_mode = SHAULA_OVERLAY_DRAG_BLOCKED;
    session->view.cursor = resolve_cursor(session, point);
    return;
  }

  if (session->view.has_selection) {
    ShaulaResizeHandle handle = resize_handle_at(session->view.selection, point);
    if (handle != HANDLE_NONE) {
      session->view.active_handle = handle;
      session->view.hover_handle = handle;
      session->view.drag_mode = SHAULA_OVERLAY_DRAG_RESIZE;
      session->view.cursor = cursor_for_handle(handle);
      return;
    }
  }
  if (session->view.has_selection && point_near_selection_border(
                                         session->view.selection, point,
                                         MOVE_HIT_RADIUS)) {
    session->view.drag_mode = SHAULA_OVERLAY_DRAG_MOVE;
    session->view.cursor = SHAULA_OVERLAY_CURSOR_GRABBING;
  } else {
    session->view.drag_mode = SHAULA_OVERLAY_DRAG_CREATE;
    session->view.cursor = SHAULA_OVERLAY_CURSOR_CROSSHAIR;
  }
}

gboolean shaula_overlay_selection_session_update(
    ShaulaOverlaySelectionSession *session, int dx, int dy) {
  g_return_val_if_fail(session != NULL, FALSE);
  ShaulaPoint point = {.x = session->drag_start.x + dx,
                       .y = session->drag_start.y + dy};
  ShaulaRect next;
  gboolean changed = FALSE;

  if (session->view.drag_mode == SHAULA_OVERLAY_DRAG_CREATE) {
    if (abs(dx) < CREATE_THRESHOLD && abs(dy) < CREATE_THRESHOLD &&
        session->view.has_selection)
      return FALSE;
    gboolean valid = geometry_from_points(
        session->drag_start, point,
        session->view.has_aspect ? session->view.aspect : (ShaulaAspect){0},
        session->bounds, &next);
    if (valid) {
      changed = !session->view.has_selection ||
                next.x != session->view.selection.x ||
                next.y != session->view.selection.y ||
                next.width != session->view.selection.width ||
                next.height != session->view.selection.height;
      session->view.selection = next;
      session->view.has_selection = TRUE;
    }
    session->view.cursor = SHAULA_OVERLAY_CURSOR_CROSSHAIR;
  } else if (session->view.drag_mode == SHAULA_OVERLAY_DRAG_MOVE) {
    if (move_selection(session->drag_origin, dx, dy, session->bounds, &next)) {
      changed = next.x != session->view.selection.x ||
                next.y != session->view.selection.y;
      session->view.selection = next;
      session->view.has_selection = TRUE;
    }
    session->view.cursor = SHAULA_OVERLAY_CURSOR_GRABBING;
  } else if (session->view.drag_mode == SHAULA_OVERLAY_DRAG_RESIZE) {
    if (resize_selection(
            session->drag_origin, session->view.active_handle, point,
            session->view.has_aspect ? session->view.aspect : (ShaulaAspect){0},
            session->bounds, &next)) {
      changed = next.x != session->view.selection.x ||
                next.y != session->view.selection.y ||
                next.width != session->view.selection.width ||
                next.height != session->view.selection.height;
      session->view.selection = next;
      session->view.has_selection = TRUE;
    }
    session->view.cursor = cursor_for_handle(session->view.active_handle);
  }
  update_confirmable(session);
  return changed;
}

gboolean shaula_overlay_selection_session_end(
    ShaulaOverlaySelectionSession *session, int dx, int dy,
    gboolean confirm_on_release) {
  g_return_val_if_fail(session != NULL, FALSE);
  ShaulaOverlayDragMode completed = session->view.drag_mode;
  (void)shaula_overlay_selection_session_update(session, dx, dy);
  gboolean should_confirm =
      confirm_on_release &&
      (completed == SHAULA_OVERLAY_DRAG_CREATE ||
       completed == SHAULA_OVERLAY_DRAG_MOVE ||
       completed == SHAULA_OVERLAY_DRAG_RESIZE) &&
      session->view.confirmable;

  ShaulaPoint point = {.x = session->drag_start.x + dx,
                       .y = session->drag_start.y + dy};
  session->view.drag_mode = SHAULA_OVERLAY_DRAG_NONE;
  session->view.active_handle = HANDLE_NONE;
  session->view.hover_handle =
      session->view.has_selection
          ? resize_handle_at(session->view.selection, point)
          : HANDLE_NONE;
  session->view.cursor = resolve_cursor(session, point);
  return should_confirm;
}

gboolean shaula_overlay_selection_session_motion(
    ShaulaOverlaySelectionSession *session, ShaulaPoint point) {
  g_return_val_if_fail(session != NULL, FALSE);
  if (session->view.drag_mode != SHAULA_OVERLAY_DRAG_NONE)
    return FALSE;
  ShaulaResizeHandle next =
      session->view.has_selection
          ? resize_handle_at(session->view.selection, point)
          : HANDLE_NONE;
  gboolean changed = next != session->view.hover_handle;
  session->view.hover_handle = next;
  session->view.cursor = resolve_cursor(session, point);
  return changed;
}

void shaula_overlay_selection_session_leave(
    ShaulaOverlaySelectionSession *session) {
  g_return_if_fail(session != NULL);
  session->view.hover_handle = HANDLE_NONE;
  session->view.cursor = SHAULA_OVERLAY_CURSOR_DEFAULT;
}

gboolean shaula_overlay_selection_session_nudge(
    ShaulaOverlaySelectionSession *session, int dx, int dy, int step) {
  g_return_val_if_fail(session != NULL, FALSE);
  if (!session->view.has_selection || (dx == 0 && dy == 0) || step <= 0)
    return FALSE;
  ShaulaRect next;
  if (!move_selection(session->view.selection, dx * step, dy * step,
                      session->bounds, &next))
    return FALSE;
  gboolean changed = next.x != session->view.selection.x ||
                     next.y != session->view.selection.y;
  session->view.selection = next;
  update_confirmable(session);
  return changed;
}
