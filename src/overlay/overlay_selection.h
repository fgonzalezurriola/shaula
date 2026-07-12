#ifndef SHAULA_OVERLAY_SELECTION_H
#define SHAULA_OVERLAY_SELECTION_H

#include <glib.h>

typedef struct { int x; int y; int width; int height; } ShaulaRect;
typedef struct { int x; int y; } ShaulaPoint;
typedef struct { int width; int height; } ShaulaAspect;
typedef enum {
  HANDLE_NONE, HANDLE_TOP_LEFT, HANDLE_TOP, HANDLE_TOP_RIGHT, HANDLE_RIGHT,
  HANDLE_BOTTOM_RIGHT, HANDLE_BOTTOM, HANDLE_BOTTOM_LEFT, HANDLE_LEFT,
} ShaulaResizeHandle;

void apply_aspect(int *width, int *height, ShaulaAspect ratio);
gboolean clamp_selection(ShaulaRect input, ShaulaPoint bounds, ShaulaRect *out);
gboolean clamp_selection_preserve_size(ShaulaRect input, ShaulaPoint bounds,
                                       ShaulaRect *out);
gboolean geometry_from_points(ShaulaPoint anchor, ShaulaPoint point,
                              ShaulaAspect aspect, ShaulaPoint bounds,
                              ShaulaRect *out);
gboolean move_selection(ShaulaRect selection, int dx, int dy,
                        ShaulaPoint bounds, ShaulaRect *out);
gboolean normalize_selection(int left, int top, int right, int bottom,
                             ShaulaPoint bounds, ShaulaRect *out);
gboolean resize_free(ShaulaRect origin, ShaulaResizeHandle handle,
                     ShaulaPoint point, ShaulaPoint bounds, ShaulaRect *out);
gboolean resize_aspect(ShaulaRect origin, ShaulaResizeHandle handle,
                       ShaulaPoint point, ShaulaAspect aspect,
                       ShaulaPoint bounds, ShaulaRect *out);
gboolean resize_selection(ShaulaRect origin, ShaulaResizeHandle handle,
                          ShaulaPoint point, ShaulaAspect aspect,
                          ShaulaPoint bounds, ShaulaRect *out);
gboolean apply_aspect_from_center(ShaulaRect input, ShaulaAspect aspect,
                                 ShaulaPoint bounds, ShaulaRect *out);
gboolean apply_aspect_from_center_preserve(ShaulaRect input,
                                          ShaulaAspect aspect,
                                          ShaulaPoint bounds,
                                          ShaulaRect *out);
#endif

