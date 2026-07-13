#ifndef SHAULA_PREVIEW_EDIT_SESSION_H
#define SHAULA_PREVIEW_EDIT_SESSION_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#include "preview_annotations.h"
#include "preview_geometry.h"

typedef struct ShaulaPreviewState ShaulaPreviewState;

typedef enum {
  SHAULA_PREVIEW_EDIT_QUERY_SELECTION,
  SHAULA_PREVIEW_EDIT_QUERY_ERASER,
} ShaulaPreviewEditQueryKind;

typedef struct {
  ShaulaPreviewEditQueryKind kind;
  ShaulaRect rect;
  ShaulaPoint start;
  ShaulaPoint end;
  double tolerance;
} ShaulaPreviewEditQuery;

typedef struct {
  gboolean clear_crop_draft;
  gboolean clear_region_selection;
  gboolean reset_tool_to_select;
  gboolean update_dimensions;
  gboolean fit_to_screen;
  gboolean queue_draw;
} ShaulaPreviewEditFinish;

/* Annotation variant dispatch stays behind the edit-session seam. */
ShaulaAnnotationHit shaula_preview_edit_hit_test(GPtrArray *annotations,
                                                 ShaulaPoint point,
                                                 double tolerance);
gboolean shaula_preview_edit_matches(const ShaulaAnnotation *annotation,
                                      ShaulaPreviewEditQuery query);

/* Owns the output-affecting edit and history lifecycle used by GTK adapters. */
void shaula_preview_edit_begin(ShaulaPreviewState *state);
void shaula_preview_edit_replace_image(ShaulaPreviewState *state,
                                       GdkPixbuf *replacement);
void shaula_preview_edit_finish(ShaulaPreviewState *state,
                                ShaulaPreviewEditFinish finish);
void shaula_preview_clear_region_selection(ShaulaPreviewState *state);
void shaula_preview_push_undo(ShaulaPreviewState *state);
void shaula_preview_begin_history_gesture(ShaulaPreviewState *state);
void shaula_preview_commit_history_gesture(ShaulaPreviewState *state,
                                           gboolean changed);
void shaula_preview_cancel_history_gesture(ShaulaPreviewState *state);
gboolean shaula_preview_can_undo(ShaulaPreviewState *state);
gboolean shaula_preview_can_redo(ShaulaPreviewState *state);
gboolean shaula_preview_undo(ShaulaPreviewState *state);
gboolean shaula_preview_redo(ShaulaPreviewState *state);
void shaula_preview_cancel_operation(ShaulaPreviewState *state);

#endif
