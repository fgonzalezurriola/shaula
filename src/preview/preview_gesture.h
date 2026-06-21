#ifndef SHAULA_PREVIEW_GESTURE_H
#define SHAULA_PREVIEW_GESTURE_H

#include <glib.h>

#include "preview_annotations.h"
#include "preview_geometry.h"

typedef struct ShaulaPreviewState ShaulaPreviewState;

typedef enum {
  SHAULA_OPERATION_NONE,
  SHAULA_OPERATION_PAN,
  SHAULA_OPERATION_MOVE,
  SHAULA_OPERATION_BEND_ARROW,
  SHAULA_OPERATION_RESIZE_ANNOTATION,
  SHAULA_OPERATION_SELECT_REGION,
  SHAULA_OPERATION_CROP,
  SHAULA_OPERATION_ERASE_ANNOTATIONS,
  SHAULA_OPERATION_ARROW,
  SHAULA_OPERATION_LINE,
  SHAULA_OPERATION_RECTANGLE,
  SHAULA_OPERATION_HIGHLIGHT,
  SHAULA_OPERATION_PEN,
  SHAULA_OPERATION_SPOTLIGHT,
  SHAULA_OPERATION_MEASURE,
  SHAULA_OPERATION_TEXT
} ShaulaPreviewOperation;

typedef enum {
  SHAULA_RESIZE_HANDLE_NONE,
  SHAULA_RESIZE_HANDLE_RECT_NW,
  SHAULA_RESIZE_HANDLE_RECT_N,
  SHAULA_RESIZE_HANDLE_RECT_NE,
  SHAULA_RESIZE_HANDLE_RECT_E,
  SHAULA_RESIZE_HANDLE_RECT_SE,
  SHAULA_RESIZE_HANDLE_RECT_S,
  SHAULA_RESIZE_HANDLE_RECT_SW,
  SHAULA_RESIZE_HANDLE_RECT_W,
  SHAULA_RESIZE_HANDLE_ARROW_START,
  SHAULA_RESIZE_HANDLE_ARROW_END,
  SHAULA_RESIZE_HANDLE_ARROW_CONTROL
} ShaulaAnnotationResizeHandle;

typedef struct {
  ShaulaAnnotation *pressed_annotation;
  gboolean preserved_multi_selection;
  ShaulaAnnotationResizeHandle resize_handle;
  ShaulaRect resize_origin_rect;
  ShaulaPoint resize_origin_arrow_start;
  ShaulaPoint resize_origin_arrow_end;
  ShaulaPoint resize_origin_arrow_control;
  gboolean resize_origin_arrow_curved;
} ShaulaPreviewGestureState;

typedef struct {
  double screen_x;
  double screen_y;
  guint button;
  gboolean shift;
} ShaulaPreviewPointerEvent;

void shaula_preview_gesture_state_init(ShaulaPreviewGestureState *gesture);
void shaula_preview_gesture_reset(ShaulaPreviewGestureState *gesture);

/* Starts generic drag state without embedding GTK event handling in the
 * interpreter. Canvas adapters normalize pointer input before crossing here.
 */
void shaula_preview_gesture_begin_operation(ShaulaPreviewState *state,
                                            ShaulaPreviewOperation operation,
                                            ShaulaPoint point);
void shaula_preview_gesture_begin_pan(ShaulaPreviewState *state,
                                      double screen_x, double screen_y);

/* Owns Select-tool hit priority and operation routing. The returned cursor is a
 * stable GTK cursor name, but the interpreter does not call GTK itself.
 */
gboolean shaula_preview_gesture_begin_selection(
    ShaulaPreviewState *state, ShaulaPreviewPointerEvent event,
    const char **cursor_name_out);

/* Applies the active non-eraser drag. Returns FALSE for operations that remain
 * runtime-specific to Canvas, such as eraser IO and text entry.
 */
gboolean shaula_preview_gesture_update(ShaulaPreviewState *state,
                                       double screen_x, double screen_y,
                                       double delta_x, double delta_y);

/* Commits Select-tool history and click-without-movement semantics. */
gboolean shaula_preview_gesture_end_selection(ShaulaPreviewState *state);

const char *shaula_preview_gesture_hover_cursor(
    ShaulaPreviewState *state, double screen_x, double screen_y);

gboolean shaula_preview_gesture_is_selection_operation(
    ShaulaPreviewOperation operation);

#endif
