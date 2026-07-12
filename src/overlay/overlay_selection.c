#include "overlay_selection.h"

#include <stdlib.h>

static int clamp_int(int value, int low, int high) {
  return value < low ? low : (value > high ? high : value);
}

void apply_aspect(int *width, int *height, ShaulaAspect ratio) {
  if (*width == 0 && *height > 0) {
    *width = MAX(1, (*height * ratio.width) / ratio.height);
  } else if (*height == 0 && *width > 0) {
    *height = MAX(1, (*width * ratio.height) / ratio.width);
  } else if (*width > 0 && *height > 0) {
    if ((*width * ratio.height) >= (*height * ratio.width)) {
      *width = MAX(1, (*height * ratio.width) / ratio.height);
    } else {
      *height = MAX(1, (*width * ratio.height) / ratio.width);
    }
  }
}

gboolean clamp_selection(ShaulaRect input, ShaulaPoint bounds,
                                ShaulaRect *out) {
  int left = clamp_int(input.x, 0, MAX(0, bounds.x - 1));
  int top = clamp_int(input.y, 0, MAX(0, bounds.y - 1));
  int right = clamp_int(input.x + input.width, 1, bounds.x);
  int bottom = clamp_int(input.y + input.height, 1, bounds.y);
  if (right <= left)
    right = MIN(bounds.x, left + 1);
  if (bottom <= top)
    bottom = MIN(bounds.y, top + 1);
  if (right <= left || bottom <= top)
    return FALSE;
  *out = (ShaulaRect){
      .x = left, .y = top, .width = right - left, .height = bottom - top};
  return TRUE;
}

gboolean clamp_selection_preserve_size(ShaulaRect input,
                                              ShaulaPoint bounds,
                                              ShaulaRect *out) {
  int width = MIN(input.width, bounds.x);
  int height = MIN(input.height, bounds.y);
  if (width <= 0 || height <= 0)
    return FALSE;
  *out = (ShaulaRect){
      .x = clamp_int(input.x, 0, MAX(0, bounds.x - width)),
      .y = clamp_int(input.y, 0, MAX(0, bounds.y - height)),
      .width = width,
      .height = height,
  };
  return TRUE;
}

gboolean geometry_from_points(ShaulaPoint anchor, ShaulaPoint point,
                              ShaulaAspect aspect, ShaulaPoint bounds,
                              ShaulaRect *out) {
  int width = abs(point.x - anchor.x) + 1;
  int height = abs(point.y - anchor.y) + 1;
  if (aspect.width > 0 && aspect.height > 0)
    apply_aspect(&width, &height, aspect);
  if (width <= 0 || height <= 0)
    return FALSE;
  int x = point.x >= anchor.x ? anchor.x : anchor.x - width;
  int y = point.y >= anchor.y ? anchor.y : anchor.y - height;
  return clamp_selection(
      (ShaulaRect){.x = x, .y = y, .width = width, .height = height}, bounds,
      out);
}

gboolean move_selection(ShaulaRect selection, int dx, int dy,
                               ShaulaPoint bounds, ShaulaRect *out) {
  *out = (ShaulaRect){
      .x = clamp_int(selection.x + dx, 0, MAX(0, bounds.x - selection.width)),
      .y = clamp_int(selection.y + dy, 0, MAX(0, bounds.y - selection.height)),
      .width = selection.width,
      .height = selection.height,
  };
  return TRUE;
}

gboolean normalize_selection(int left, int top, int right, int bottom,
                                    ShaulaPoint bounds, ShaulaRect *out) {
  int x0 = MIN(left, right);
  int x1 = MAX(left, right);
  int y0 = MIN(top, bottom);
  int y1 = MAX(top, bottom);
  if (x1 <= x0)
    x1 = x0 + 1;
  if (y1 <= y0)
    y1 = y0 + 1;
  return clamp_selection(
      (ShaulaRect){.x = x0, .y = y0, .width = x1 - x0, .height = y1 - y0},
      bounds, out);
}

gboolean resize_free(ShaulaRect origin, ShaulaResizeHandle handle,
                            ShaulaPoint point, ShaulaPoint bounds,
                            ShaulaRect *out) {
  int left = origin.x;
  int top = origin.y;
  int right = origin.x + origin.width;
  int bottom = origin.y + origin.height;

  switch (handle) {
  case HANDLE_TOP_LEFT:
    left = point.x;
    top = point.y;
    break;
  case HANDLE_TOP:
    top = point.y;
    break;
  case HANDLE_TOP_RIGHT:
    right = point.x + 1;
    top = point.y;
    break;
  case HANDLE_RIGHT:
    right = point.x + 1;
    break;
  case HANDLE_BOTTOM_RIGHT:
    right = point.x + 1;
    bottom = point.y + 1;
    break;
  case HANDLE_BOTTOM:
    bottom = point.y + 1;
    break;
  case HANDLE_BOTTOM_LEFT:
    left = point.x;
    bottom = point.y + 1;
    break;
  case HANDLE_LEFT:
    left = point.x;
    break;
  default:
    return FALSE;
  }

  return normalize_selection(left, top, right, bottom, bounds, out);
}

gboolean resize_aspect(ShaulaRect origin, ShaulaResizeHandle handle,
                       ShaulaPoint point, ShaulaAspect aspect,
                       ShaulaPoint bounds, ShaulaRect *out) {
  int left = origin.x;
  int top = origin.y;
  int right = origin.x + origin.width;
  int bottom = origin.y + origin.height;
  int center_x = origin.x + origin.width / 2;
  int center_y = origin.y + origin.height / 2;
  int width = origin.width;
  int height = origin.height;

  switch (handle) {
  case HANDLE_TOP_LEFT:
    return geometry_from_points((ShaulaPoint){.x = right, .y = bottom}, point,
                                aspect, bounds, out);
  case HANDLE_TOP_RIGHT:
    return geometry_from_points((ShaulaPoint){.x = left, .y = bottom}, point,
                                aspect, bounds, out);
  case HANDLE_BOTTOM_RIGHT:
    return geometry_from_points((ShaulaPoint){.x = left, .y = top}, point,
                                aspect, bounds, out);
  case HANDLE_BOTTOM_LEFT:
    return geometry_from_points((ShaulaPoint){.x = right, .y = top}, point,
                                aspect, bounds, out);
  case HANDLE_LEFT:
    width = abs(right - point.x);
    height = MAX(1, (width * aspect.height) / aspect.width);
    return normalize_selection(right - width, center_y - height / 2, right,
                               center_y + (height + 1) / 2, bounds, out);
  case HANDLE_RIGHT:
    width = abs(point.x + 1 - left);
    height = MAX(1, (width * aspect.height) / aspect.width);
    return normalize_selection(left, center_y - height / 2, left + width,
                               center_y + (height + 1) / 2, bounds, out);
  case HANDLE_TOP:
    height = abs(bottom - point.y);
    width = MAX(1, (height * aspect.width) / aspect.height);
    return normalize_selection(center_x - width / 2, bottom - height,
                               center_x + (width + 1) / 2, bottom, bounds, out);
  case HANDLE_BOTTOM:
    height = abs(point.y + 1 - top);
    width = MAX(1, (height * aspect.width) / aspect.height);
    return normalize_selection(center_x - width / 2, top,
                               center_x + (width + 1) / 2, top + height, bounds,
                               out);
  default:
    return FALSE;
  }
}

gboolean resize_selection(ShaulaRect origin, ShaulaResizeHandle handle,
                          ShaulaPoint point, ShaulaAspect aspect,
                          ShaulaPoint bounds, ShaulaRect *out) {
  if (aspect.width > 0 && aspect.height > 0)
    return resize_aspect(origin, handle, point, aspect, bounds, out);
  return resize_free(origin, handle, point, bounds, out);
}

gboolean apply_aspect_from_center(ShaulaRect input, ShaulaAspect aspect,
                                         ShaulaPoint bounds, ShaulaRect *out) {
  int w = input.width;
  int h = input.height;
  apply_aspect(&w, &h, aspect);
  int cx = input.x + input.width / 2;
  int cy = input.y + input.height / 2;
  ShaulaRect centered = {
      .x = cx - w / 2,
      .y = cy - h / 2,
      .width = w,
      .height = h,
  };
  return clamp_selection(centered, bounds, out);
}

gboolean apply_aspect_from_center_preserve(ShaulaRect input,
                                                  ShaulaAspect aspect,
                                                  ShaulaPoint bounds,
                                                  ShaulaRect *out) {
  int w = input.width;
  int h = input.height;
  apply_aspect(&w, &h, aspect);
  int cx = input.x + input.width / 2;
  int cy = input.y + input.height / 2;
  ShaulaRect centered = {
      .x = cx - w / 2,
      .y = cy - h / 2,
      .width = w,
      .height = h,
  };
  return clamp_selection_preserve_size(centered, bounds, out);
}

