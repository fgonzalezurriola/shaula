#include "preview_measure.h"

#include <math.h>
#include <string.h>

static gboolean pixels_close_sum(const guchar *row_start, int n_channels,
	const guchar *ref, int tolerance) {
	int diff = 0;
	for (int c = 0; c < n_channels; c++)
		diff += abs((int)row_start[c] - (int)ref[c]);
	return diff <= tolerance * 3;
}

static gboolean pixels_close_per_channel(const guchar *row_start, int n_channels,
	const guchar *ref, int tolerance) {
	for (int c = 0; c < n_channels; c++) {
		if (abs((int)row_start[c] - (int)ref[c]) > tolerance)
			return FALSE;
	}
	return TRUE;
}

typedef gboolean (*PixelsCloseFn)(const guchar *, int, const guchar *, int);

static int find_edge_left(GdkPixbuf *image, int px, int py,
	int tolerance, PixelsCloseFn close_fn) {
	int n_channels = gdk_pixbuf_get_n_channels(image);
	int rowstride = gdk_pixbuf_get_rowstride(image);
	guchar *pixels = gdk_pixbuf_get_pixels(image);
	guchar *row = pixels + py * rowstride;
	guchar *ref = row + px * n_channels;
	for (int x = px - 1; x >= 0; x--) {
		if (!close_fn(row + x * n_channels, n_channels, ref, tolerance))
			return x + 1;
	}
	return 0;
}

static int find_edge_right(GdkPixbuf *image, int px, int py,
	int tolerance, PixelsCloseFn close_fn) {
	int width = gdk_pixbuf_get_width(image);
	int n_channels = gdk_pixbuf_get_n_channels(image);
	int rowstride = gdk_pixbuf_get_rowstride(image);
	guchar *pixels = gdk_pixbuf_get_pixels(image);
	guchar *ref = pixels + py * rowstride + px * n_channels;
	for (int x = px + 1; x < width; x++) {
		guchar *row = pixels + py * rowstride;
		if (!close_fn(row + x * n_channels, n_channels, ref, tolerance))
			return x - 1;
	}
	return width - 1;
}

static int find_edge_up(GdkPixbuf *image, int px, int py,
	int tolerance, PixelsCloseFn close_fn) {
	int n_channels = gdk_pixbuf_get_n_channels(image);
	int rowstride = gdk_pixbuf_get_rowstride(image);
	guchar *pixels = gdk_pixbuf_get_pixels(image);
	guchar *ref = pixels + py * rowstride + px * n_channels;
	for (int y = py - 1; y >= 0; y--) {
		if (!close_fn(pixels + y * rowstride + px * n_channels, n_channels, ref, tolerance))
			return y + 1;
	}
	return 0;
}

static int find_edge_down(GdkPixbuf *image, int px, int py,
	int tolerance, PixelsCloseFn close_fn) {
	int height = gdk_pixbuf_get_height(image);
	int n_channels = gdk_pixbuf_get_n_channels(image);
	int rowstride = gdk_pixbuf_get_rowstride(image);
	guchar *pixels = gdk_pixbuf_get_pixels(image);
	guchar *ref = pixels + py * rowstride + px * n_channels;
	for (int y = py + 1; y < height; y++) {
		if (!close_fn(pixels + y * rowstride + px * n_channels, n_channels, ref, tolerance))
			return y - 1;
	}
	return height - 1;
}

static int find_outer_left(GdkPixbuf *image, int px, int py,
	int tolerance, PixelsCloseFn close_fn) {
	int n_channels = gdk_pixbuf_get_n_channels(image);
	int rowstride = gdk_pixbuf_get_rowstride(image);
	guchar *pixels = gdk_pixbuf_get_pixels(image);
	int inner = find_edge_left(image, px, py, tolerance, close_fn);
	if (inner <= 0)
		return 0;
	guchar *ref = pixels + py * rowstride + inner * n_channels;
	for (int x = inner - 1; x >= 0; x--) {
		guchar *row = pixels + py * rowstride;
		if (!close_fn(row + x * n_channels, n_channels, ref, tolerance))
			return x + 1;
	}
	return 0;
}

static int find_outer_right(GdkPixbuf *image, int px, int py,
	int tolerance, PixelsCloseFn close_fn) {
	int width = gdk_pixbuf_get_width(image);
	int n_channels = gdk_pixbuf_get_n_channels(image);
	int rowstride = gdk_pixbuf_get_rowstride(image);
	guchar *pixels = gdk_pixbuf_get_pixels(image);
	int inner = find_edge_right(image, px, py, tolerance, close_fn);
	if (inner >= width - 1)
		return width - 1;
	guchar *ref = pixels + py * rowstride + inner * n_channels;
	for (int x = inner + 1; x < width; x++) {
		guchar *row = pixels + py * rowstride;
		if (!close_fn(row + x * n_channels, n_channels, ref, tolerance))
			return x - 1;
	}
	return width - 1;
}

static int find_outer_up(GdkPixbuf *image, int px, int py,
	int tolerance, PixelsCloseFn close_fn) {
	int n_channels = gdk_pixbuf_get_n_channels(image);
	int rowstride = gdk_pixbuf_get_rowstride(image);
	guchar *pixels = gdk_pixbuf_get_pixels(image);
	int inner = find_edge_up(image, px, py, tolerance, close_fn);
	if (inner <= 0)
		return 0;
	guchar *ref = pixels + inner * rowstride + px * n_channels;
	for (int y = inner - 1; y >= 0; y--) {
		if (!close_fn(pixels + y * rowstride + px * n_channels, n_channels, ref, tolerance))
			return y + 1;
	}
	return 0;
}

static int find_outer_down(GdkPixbuf *image, int px, int py,
	int tolerance, PixelsCloseFn close_fn) {
	int height = gdk_pixbuf_get_height(image);
	int n_channels = gdk_pixbuf_get_n_channels(image);
	int rowstride = gdk_pixbuf_get_rowstride(image);
	guchar *pixels = gdk_pixbuf_get_pixels(image);
	int inner = find_edge_down(image, px, py, tolerance, close_fn);
	if (inner >= height - 1)
		return height - 1;
	guchar *ref = pixels + inner * rowstride + px * n_channels;
	for (int y = inner + 1; y < height; y++) {
		if (!close_fn(pixels + y * rowstride + px * n_channels, n_channels, ref, tolerance))
			return y - 1;
	}
	return height - 1;
}

void shaula_measure_detect_edges(GdkPixbuf *image, int px, int py,
	int tolerance, ShaulaMeasurePixelCompare compare_mode,
	ShaulaMeasureMode direction, gboolean outer_bounds,
	ShaulaMeasureResult *result) {
	memset(result, 0, sizeof(*result));
	if (image == NULL || result == NULL)
		return;
	int width = gdk_pixbuf_get_width(image);
	int height = gdk_pixbuf_get_height(image);
	if (px < 0 || px >= width || py < 0 || py >= height)
		return;

	PixelsCloseFn close_fn = compare_mode == SHAULA_MEASURE_PIXEL_COMPARE_PER_CHANNEL
		? pixels_close_per_channel
		: pixels_close_sum;

	if (direction == SHAULA_MEASURE_MODE_HORIZONTAL) {
		if (outer_bounds) {
			result->left = find_outer_left(image, px, py, tolerance, close_fn);
			result->right = find_outer_right(image, px, py, tolerance, close_fn);
		} else {
			result->left = find_edge_left(image, px, py, tolerance, close_fn);
			result->right = find_edge_right(image, px, py, tolerance, close_fn);
		}
		result->top = py;
		result->bottom = py;
		result->width = result->right - result->left + 1;
		result->height = 1;
	} else if (direction == SHAULA_MEASURE_MODE_VERTICAL) {
		result->left = px;
		result->right = px;
		if (outer_bounds) {
			result->top = find_outer_up(image, px, py, tolerance, close_fn);
			result->bottom = find_outer_down(image, px, py, tolerance, close_fn);
		} else {
			result->top = find_edge_up(image, px, py, tolerance, close_fn);
			result->bottom = find_edge_down(image, px, py, tolerance, close_fn);
		}
		result->width = 1;
		result->height = result->bottom - result->top + 1;
	} else {
		if (outer_bounds) {
			result->left = find_outer_left(image, px, py, tolerance, close_fn);
			result->right = find_outer_right(image, px, py, tolerance, close_fn);
			result->top = find_outer_up(image, px, py, tolerance, close_fn);
			result->bottom = find_outer_down(image, px, py, tolerance, close_fn);
		} else {
			result->left = find_edge_left(image, px, py, tolerance, close_fn);
			result->right = find_edge_right(image, px, py, tolerance, close_fn);
			result->top = find_edge_up(image, px, py, tolerance, close_fn);
			result->bottom = find_edge_down(image, px, py, tolerance, close_fn);
		}
		result->width = result->right - result->left + 1;
		result->height = result->bottom - result->top + 1;
	}
}
