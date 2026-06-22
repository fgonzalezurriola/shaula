#include "preview_document.h"
#include "preview_paste_placement.h"

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

static void assert_paste_placement(void) {
  ShaulaPastePlacement placement;
  require_true(shaula_paste_calculate_placement(
      100.0, 50.0, (ShaulaRect){0, 0, 400, 300},
      (ShaulaRect){0, 0, 400, 300}, 20.0, &placement));
  require_close(placement.x, 150.0);
  require_close(placement.y, 125.0);
  require_close(placement.width, 100.0);
  require_close(placement.height, 50.0);
  require_close(placement.scale, 1.0);

  require_true(shaula_paste_calculate_placement(
      800.0, 400.0, (ShaulaRect){0, 0, 400, 300},
      (ShaulaRect){0, 0, 400, 300}, 20.0, &placement));
  require_close(placement.x, 20.0);
  require_close(placement.y, 60.0);
  require_close(placement.width, 360.0);
  require_close(placement.height, 180.0);
  require_close(placement.scale, 0.45);
  require_close(placement.width / placement.height, 2.0);

  require_true(shaula_paste_calculate_placement(
      40.0, 80.0, (ShaulaRect){-100, -100, 150, 150},
      (ShaulaRect){0, 0, 60, 60}, 16.0, &placement));
  require_true(isfinite(placement.x) && isfinite(placement.y));
  require_true(placement.width > 0.0 && placement.height > 0.0);
  require_true(placement.x >= 0.0 && placement.y >= 0.0);
  require_true(placement.x + placement.width <= 60.0);
  require_true(placement.y + placement.height <= 60.0);

  require_true(!shaula_paste_calculate_placement(
      0.0, 20.0, (ShaulaRect){0, 0, 100, 100},
      (ShaulaRect){0, 0, 100, 100}, 8.0, &placement));
  require_true(!shaula_paste_calculate_placement(
      NAN, 20.0, (ShaulaRect){0, 0, 100, 100},
      (ShaulaRect){0, 0, 100, 100}, 8.0, &placement));
  require_true(!shaula_paste_calculate_placement(
      20.0, 20.0, (ShaulaRect){NAN, 0, 100, 100},
      (ShaulaRect){0, 0, 100, 100}, 8.0, &placement));

  ShaulaPoint translation = shaula_paste_clamp_bounds_translation(
      (ShaulaRect){-30, 95, 80, 30}, (ShaulaRect){0, 0, 100, 100}, 10.0);
  require_close(translation.x, 40.0);
  require_close(translation.y, -35.0);
  translation = shaula_paste_clamp_bounds_translation(
      (ShaulaRect){NAN, 0, 20, 20}, (ShaulaRect){0, 0, 100, 100}, 10.0);
  require_close(translation.x, 0.0);
  require_close(translation.y, 0.0);
}

static void assert_paste_validation(void) {
  require_true(shaula_paste_validate_text("hello\nworld", 100) ==
               SHAULA_PASTE_TEXT_VALID);
  require_true(shaula_paste_validate_text(" \n\t", 100) ==
               SHAULA_PASTE_TEXT_EMPTY);
  require_true(shaula_paste_validate_text("\xE3\x80\x80", 100) ==
               SHAULA_PASTE_TEXT_EMPTY);
  require_true(shaula_paste_validate_text("abcdef", 5) ==
               SHAULA_PASTE_TEXT_TOO_LARGE);
  require_true(shaula_paste_validate_text("\xff", 100) ==
               SHAULA_PASTE_TEXT_INVALID_UTF8);
  require_true(shaula_paste_validate_image_dimensions(100, 200, 1000,
                                                      1000000));
  require_true(!shaula_paste_validate_image_dimensions(0, 200, 1000,
                                                       1000000));
  require_true(!shaula_paste_validate_image_dimensions(1001, 200, 1000,
                                                       1000000));
  require_true(!shaula_paste_validate_image_dimensions(1000, 1000, 2000,
                                                       999999));
}

static void assert_image_annotation_contract(void) {
  GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 4, 4);
  require_true(pixbuf != NULL);
  gdk_pixbuf_fill(pixbuf, 0x112233ff);
  guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
  pixels[0] = 0xaa;

  ShaulaAnnotation *image = shaula_annotation_new_image_take(
      pixbuf, (ShaulaRect){10, 20, 40, 40});
  require_true(image != NULL);
  require_true(image->type == SHAULA_ANNOTATION_IMAGE);
  require_close(image->bounds.x, 10.0);
  require_close(image->bounds.y, 20.0);

  ShaulaAnnotation *clone = shaula_annotation_clone(image);
  require_true(clone != NULL);
  require_true(clone->data.image.pixbuf != image->data.image.pixbuf);
  require_true(gdk_pixbuf_get_pixels(clone->data.image.pixbuf)[0] == 0xaa);
  gdk_pixbuf_get_pixels(image->data.image.pixbuf)[0] = 0x44;
  require_true(gdk_pixbuf_get_pixels(clone->data.image.pixbuf)[0] == 0xaa);

  assert_bounds_translate(image, 5.0, -10.0);
  require_close(image->data.image.rect.x, 15.0);
  require_close(image->data.image.rect.y, 10.0);

  require_true(shaula_annotation_apply_document_crop(
      clone, (ShaulaRect){20, 30, 20, 20}));
  require_close(clone->data.image.rect.x, 0.0);
  require_close(clone->data.image.rect.y, 0.0);
  require_close(clone->data.image.rect.width, 20.0);
  require_close(clone->data.image.rect.height, 20.0);
  require_true(gdk_pixbuf_get_width(clone->data.image.pixbuf) == 2);
  require_true(gdk_pixbuf_get_height(clone->data.image.pixbuf) == 2);

  shaula_annotation_free(clone);
  shaula_annotation_free(image);
}

static void assert_image_snapshot_contract(ShaulaPreviewDocument *document) {
  GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 2, 2);
  require_true(pixbuf != NULL);
  gdk_pixbuf_fill(pixbuf, 0x334455ff);
  gdk_pixbuf_get_pixels(pixbuf)[0] = 0x77;
  ShaulaAnnotation *image = shaula_annotation_new_image_take(
      pixbuf, (ShaulaRect){0, 0, 2, 2});
  require_true(image != NULL);
  shaula_preview_document_add_annotation(document, image);

  ShaulaPreviewSnapshot *snapshot =
      shaula_preview_document_snapshot_new(document);
  require_true(snapshot != NULL);
  gdk_pixbuf_get_pixels(image->data.image.pixbuf)[0] = 0x11;
  shaula_preview_document_restore_snapshot(document, snapshot);
  require_true(document->annotations->len == 1);
  ShaulaAnnotation *restored = g_ptr_array_index(document->annotations, 0);
  require_true(restored->type == SHAULA_ANNOTATION_IMAGE);
  require_true(gdk_pixbuf_get_pixels(restored->data.image.pixbuf)[0] == 0x77);
  shaula_preview_snapshot_free(snapshot);
  require_true(shaula_preview_document_clear_annotations(document));
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

  require_true(shaula_preview_document_clear_annotations(&document));
  require_true(document.annotations->len == 0);

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

  assert_paste_placement();
  assert_paste_validation();
  assert_image_annotation_contract();
  assert_image_snapshot_contract(&document);
  assert_selected_group_bounds();

  shaula_preview_document_free(&document);
  return 0;
}
