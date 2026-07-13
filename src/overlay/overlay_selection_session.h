#ifndef SHAULA_OVERLAY_SELECTION_SESSION_H
#define SHAULA_OVERLAY_SELECTION_SESSION_H

#include "overlay_selection.h"

typedef enum {
  SHAULA_OVERLAY_DRAG_NONE,
  SHAULA_OVERLAY_DRAG_CREATE,
  SHAULA_OVERLAY_DRAG_MOVE,
  SHAULA_OVERLAY_DRAG_RESIZE,
  SHAULA_OVERLAY_DRAG_BLOCKED,
} ShaulaOverlayDragMode;

typedef enum {
  SHAULA_OVERLAY_CURSOR_DEFAULT,
  SHAULA_OVERLAY_CURSOR_CROSSHAIR,
  SHAULA_OVERLAY_CURSOR_GRAB,
  SHAULA_OVERLAY_CURSOR_GRABBING,
  SHAULA_OVERLAY_CURSOR_RESIZE_EW,
  SHAULA_OVERLAY_CURSOR_RESIZE_NS,
  SHAULA_OVERLAY_CURSOR_RESIZE_NWSE,
  SHAULA_OVERLAY_CURSOR_RESIZE_NESW,
} ShaulaOverlayCursor;

typedef struct {
  gboolean has_selection;
  ShaulaRect selection;
  gboolean has_aspect;
  ShaulaAspect aspect;
  ShaulaOverlayDragMode drag_mode;
  ShaulaResizeHandle active_handle;
  ShaulaResizeHandle hover_handle;
  ShaulaOverlayCursor cursor;
  gboolean confirmable;
} ShaulaOverlaySelectionView;

typedef struct {
  ShaulaPoint bounds;
  ShaulaOverlaySelectionView view;
  ShaulaPoint drag_start;
  ShaulaRect drag_origin;
} ShaulaOverlaySelectionSession;

/* Owns selection interaction ordering; GTK supplies input and renders the view. */
void shaula_overlay_selection_session_init(
    ShaulaOverlaySelectionSession *session, ShaulaPoint bounds);
void shaula_overlay_selection_session_set_bounds(
    ShaulaOverlaySelectionSession *session, ShaulaPoint bounds);
const ShaulaOverlaySelectionView *shaula_overlay_selection_session_view(
    const ShaulaOverlaySelectionSession *session);

gboolean shaula_overlay_selection_session_set_selection(
    ShaulaOverlaySelectionSession *session, ShaulaRect selection,
    gboolean preserve_size);
gboolean shaula_overlay_selection_session_set_aspect(
    ShaulaOverlaySelectionSession *session, gboolean enabled,
    ShaulaAspect aspect, gboolean preserve_size);

void shaula_overlay_selection_session_begin(
    ShaulaOverlaySelectionSession *session, ShaulaPoint point,
    gboolean blocked);
gboolean shaula_overlay_selection_session_update(
    ShaulaOverlaySelectionSession *session, int dx, int dy);
gboolean shaula_overlay_selection_session_end(
    ShaulaOverlaySelectionSession *session, int dx, int dy,
    gboolean confirm_on_release);
gboolean shaula_overlay_selection_session_motion(
    ShaulaOverlaySelectionSession *session, ShaulaPoint point);
void shaula_overlay_selection_session_leave(
    ShaulaOverlaySelectionSession *session);
gboolean shaula_overlay_selection_session_nudge(
    ShaulaOverlaySelectionSession *session, int dx, int dy, int step);

#endif
