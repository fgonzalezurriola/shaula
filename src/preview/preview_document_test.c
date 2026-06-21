#include "preview_document.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void require_close(double actual, double expected) {
  if (fabs(actual - expected) >= 0.0001)
    abort();
}

static void require_true(gboolean condition) {
  if (!condition)
    abort();
}

static void assert_bounds_translate(ShaulaAnnotation *annotation, double dx,
                                    double dy) {
  ShaulaRect before = annotation->bounds;
  shaula_annotation_move(annotation, dx, dy);
  require_close(annotation->bounds.x, before.x + dx);
  require_close(annotation->bounds.y, before.y + dy);
  require_close(annotation->bounds.width, before.width);
  require_close(annotation->bounds.height, before.height);
}

static void assert_selected_group_bounds(void) {
  GPtrArray *annotations =
      g_ptr_array_new_with_free_func(shaula_annotation_free);
  ShaulaAnnotation *left = shaula_annotation_new_rectangle(
      (ShaulaRect){20, 30, 40, 25}, shaula_color_default(), 3.0);
  ShaulaPoint path_points[] = {{120, 80}, {150, 115}, {175, 90}};
  ShaulaAnnotation *right = shaula_annotation_new_pen(
      path_points, 3, shaula_color_default(), 6.0);
  ShaulaAnnotation *ignored = shaula_annotation_new_rectangle(
      (ShaulaRect){500, 500, 40, 40}, shaula_color_default(), 3.0);
  left->selected = TRUE;
  right->selected = TRUE;
  ignored->selected = FALSE;
  g_ptr_array_add(annotations, left);
  g_ptr_array_add(annotations, right);
  g_ptr_array_add(annotations, ignored);

  double expected_x = MIN(left->bounds.x, right->bounds.x);
  double expected_y = MIN(left->bounds.y, right->bounds.y);
  double expected_right =
      MAX(left->bounds.x + left->bounds.width,
          right->bounds.x + right->bounds.width);
  double expected_bottom =
      MAX(left->bounds.y + left->bounds.height,
          right->bounds.y + right->bounds.height);
  ShaulaRect group_bounds;
  require_true(
      shaula_annotations_selected_bounds(annotations, &group_bounds));
  require_close(group_bounds.x, expected_x);
  require_close(group_bounds.y, expected_y);
  require_close(group_bounds.width, expected_right - expected_x);
  require_close(group_bounds.height, expected_bottom - expected_y);

  ShaulaRect before = group_bounds;
  shaula_annotation_move(left, 70.0, 45.0);
  shaula_annotation_move(right, 70.0, 45.0);
  require_true(
      shaula_annotations_selected_bounds(annotations, &group_bounds));
  require_close(group_bounds.x, before.x + 70.0);
  require_close(group_bounds.y, before.y + 45.0);
  require_close(group_bounds.width, before.width);
  require_close(group_bounds.height, before.height);

  left->selected = FALSE;
  right->selected = FALSE;
  require_true(
      !shaula_annotations_selected_bounds(annotations, &group_bounds));
  g_ptr_array_unref(annotations);
}

int main(void) {
  GdkPixbuf *image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 4, 3);
  assert(image != NULL);

  ShaulaPreviewDocument document;
  memset(&document, 0, sizeof(document));
  shaula_preview_document_init(&document, "/tmp/shaula-preview-doc-test.png",
                               image);

  assert(shaula_preview_document_width(&document) == 4);
  assert(shaula_preview_document_height(&document) == 3);
  assert(!shaula_preview_document_has_modifications(&document));

  ShaulaAnnotation *annotation = shaula_annotation_new_rectangle(
      (ShaulaRect){.x = 0, .y = 0, .width = 2, .height = 2},
      shaula_color_default(), 2.0);
  assert(annotation != NULL);
  shaula_preview_document_add_annotation(&document, annotation);
  assert(document.annotations->len == 1);
  assert(shaula_preview_document_has_modifications(&document));

  assert(shaula_preview_document_clear_annotations(&document));
  assert(document.annotations->len == 0);

  ShaulaPoint path_points[] = {{40, 50}, {80, 95}, {125, 60}};
  ShaulaAnnotation *pen = shaula_annotation_new_pen(
      path_points, 3, shaula_color_default(), 6.0);
  assert_bounds_translate(pen, 170.0, 90.0);
  shaula_annotation_free(pen);

  ShaulaAnnotation *highlight = shaula_annotation_new_highlight(
      path_points, 3, shaula_color_default(), 24.0);
  assert_bounds_translate(highlight, 210.0, 130.0);
  shaula_annotation_free(highlight);

  ShaulaAnnotation *arrow = shaula_annotation_new_arrow(
      (ShaulaPoint){60, 70}, (ShaulaPoint){180, 145},
      shaula_color_default(), 8.0);
  assert_bounds_translate(arrow, 115.0, 65.0);
  shaula_annotation_free(arrow);

  ShaulaAnnotation *line = shaula_annotation_new_arrow(
      (ShaulaPoint){50, 160}, (ShaulaPoint){210, 105},
      shaula_color_default(), 5.0);
  line->data.arrow.has_head = FALSE;
  shaula_annotation_update_bounds(line);
  assert_bounds_translate(line, 95.0, 40.0);
  shaula_annotation_free(line);

  ShaulaAnnotation *curved_arrow = shaula_annotation_new_arrow(
      (ShaulaPoint){30, 40}, (ShaulaPoint){220, 110},
      shaula_color_default(), 7.0);
  curved_arrow->data.arrow.is_curved = TRUE;
  curved_arrow->data.arrow.control = (ShaulaPoint){135, 240};
  shaula_annotation_update_bounds(curved_arrow);
  assert_bounds_translate(curved_arrow, 140.0, 80.0);
  shaula_annotation_free(curved_arrow);

  assert_selected_group_bounds();

  shaula_preview_document_free(&document);
  return 0;
}
