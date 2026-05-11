#ifndef SHAULA_PREVIEW_MEASURE_H
#define SHAULA_PREVIEW_MEASURE_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#include "preview_geometry.h"

typedef enum {
	SHAULA_MEASURE_MODE_AUTO,
	SHAULA_MEASURE_MODE_HORIZONTAL,
	SHAULA_MEASURE_MODE_VERTICAL,
} ShaulaMeasureMode;

typedef enum {
	SHAULA_MEASURE_PIXEL_COMPARE_SUM,
	SHAULA_MEASURE_PIXEL_COMPARE_PER_CHANNEL,
} ShaulaMeasurePixelCompare;

typedef struct {
	int left;
	int top;
	int right;
	int bottom;
	int width;
	int height;
} ShaulaMeasureResult;

void shaula_measure_detect_edges(GdkPixbuf *image, int px, int py,
	int tolerance, ShaulaMeasurePixelCompare compare_mode,
	ShaulaMeasureMode direction, gboolean outer_bounds,
	ShaulaMeasureResult *result);

#endif
