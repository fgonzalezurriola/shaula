#ifndef SHAULA_PREVIEW_TOOL_DEFAULTS_H
#define SHAULA_PREVIEW_TOOL_DEFAULTS_H

#include <glib.h>

#include "preview_annotations.h"
#include "preview_geometry.h"

#define SHAULA_ERASER_SIZE_MIN 8.0
#define SHAULA_ERASER_SIZE_MAX 48.0
#define SHAULA_ERASER_SIZE_DEFAULT 14.0
#define SHAULA_ERASER_SIZE_STEP 2.0

typedef enum {
  SHAULA_SPOTLIGHT_SHAPE_SHARP_RECTANGLE,
  SHAULA_SPOTLIGHT_SHAPE_ROUNDED_RECTANGLE
} ShaulaSpotlightShape;

typedef struct {
  ShaulaColor color;
  double stroke_width;
  PreviewArrowStrokeStyle stroke_style;
} ShaulaStrokeToolDefaults;

typedef struct {
  ShaulaColor color;
  double stroke_width;
  PreviewArrowStrokeStyle stroke_style;
  gboolean filled;
  PreviewRectangleCorners corners;
} ShaulaRectangleToolDefaults;

typedef struct {
  ShaulaColor color;
  double stroke_width;
  double opacity;
} ShaulaFreehandToolDefaults;

typedef struct {
  ShaulaColor color;
  double font_size;
  ShaulaTextAlign align;
  ShaulaTextFontMode font_mode;
} ShaulaTextToolDefaults;

typedef struct {
  ShaulaColor color;
  double stroke_width;
} ShaulaMeasureToolDefaults;

typedef struct {
  ShaulaColor border_color;
  double border_width;
  ShaulaSpotlightShape shape;
} ShaulaSpotlightToolDefaults;

typedef struct {
  double size;
} ShaulaEraserToolDefaults;

typedef enum {
  SHAULA_TOOL_DEFAULTS_DIRTY_ARROW_LINE = 1u << 0,
  SHAULA_TOOL_DEFAULTS_DIRTY_RECTANGLE = 1u << 1,
  SHAULA_TOOL_DEFAULTS_DIRTY_PEN = 1u << 2,
  SHAULA_TOOL_DEFAULTS_DIRTY_HIGHLIGHT = 1u << 3,
  SHAULA_TOOL_DEFAULTS_DIRTY_TEXT = 1u << 4,
  SHAULA_TOOL_DEFAULTS_DIRTY_MEASURE = 1u << 5,
  SHAULA_TOOL_DEFAULTS_DIRTY_SPOTLIGHT = 1u << 6,
  SHAULA_TOOL_DEFAULTS_DIRTY_ERASER = 1u << 7,
} ShaulaToolDefaultsDirtyGroup;

typedef struct {
  ShaulaStrokeToolDefaults arrow_line;
  ShaulaRectangleToolDefaults rectangle;
  ShaulaFreehandToolDefaults pen;
  ShaulaFreehandToolDefaults highlight;
  ShaulaTextToolDefaults text;
  ShaulaMeasureToolDefaults measure;
  ShaulaSpotlightToolDefaults spotlight;
  ShaulaEraserToolDefaults eraser;

  guint dirty_groups;
  guint save_timeout_id;
} ShaulaPreviewToolDefaults;

void shaula_tool_defaults_init(ShaulaPreviewToolDefaults *defaults);
void shaula_tool_defaults_dispose(ShaulaPreviewToolDefaults *defaults);
void shaula_tool_defaults_mark_dirty(ShaulaPreviewToolDefaults *defaults,
                                     ShaulaToolDefaultsDirtyGroup group);
void shaula_tool_defaults_flush(ShaulaPreviewToolDefaults *defaults);

#endif
