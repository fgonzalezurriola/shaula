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

static void assert_image_resize_contract(void) {
  GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 8, 4);
  require_true(pixbuf != NULL);
  ShaulaAnnotation *image = shaula_annotation_new_image_take(
      pixbuf, (ShaulaRect){50, 50, 80, 40});
  require_true(image != NULL);
  GdkPixbuf *owned_pixbuf = image->data.image.pixbuf;
  ShaulaRect origin = image->data.image.rect;

  require_true(shaula_annotation_resize_image_from_origin(
      image, origin, (ShaulaPoint){130, 90}, (ShaulaPoint){50, 50},
      (ShaulaPoint){10, 30}, (ShaulaRect){0, 0, 200, 120}, 16.0));
  require_close(image->data.image.rect.x, 10.0);
  require_close(image->data.image.rect.y, 30.0);
  require_close(image->data.image.rect.width, 120.0);
  require_close(image->data.image.rect.height, 60.0);
  require_close(image->data.image.rect.width / image->data.image.rect.height,
                2.0);
  require_close(image->data.image.rect.x + image->data.image.rect.width,
                130.0);
  require_close(image->data.image.rect.y + image->data.image.rect.height,
                90.0);
  require_true(image->data.image.pixbuf == owned_pixbuf);

  require_true(shaula_annotation_resize_image_from_origin(
      image, origin, (ShaulaPoint){130, 90}, (ShaulaPoint){50, 50},
      (ShaulaPoint){140, 95}, (ShaulaRect){0, 0, 200, 120}, 16.0));
  require_close(image->data.image.rect.width, 32.0);
  require_close(image->data.image.rect.height, 16.0);
  require_close(image->data.image.rect.x + image->data.image.rect.width,
                130.0);
  require_close(image->data.image.rect.y + image->data.image.rect.height,
                90.0);

  require_true(shaula_annotation_resize_image_from_origin(
      image, origin, (ShaulaPoint){50, 50}, (ShaulaPoint){130, 90},
      (ShaulaPoint){500, 500}, (ShaulaRect){0, 0, 200, 120}, 16.0));
  require_close(image->data.image.rect.x, 50.0);
  require_close(image->data.image.rect.y, 50.0);
  require_close(image->data.image.rect.width, 140.0);
  require_close(image->data.image.rect.height, 70.0);
  require_true(image->data.image.pixbuf == owned_pixbuf);

  ShaulaAnnotation *clone = shaula_annotation_clone(image);
  require_true(clone != NULL);
  require_close(clone->data.image.rect.x, 50.0);
  require_close(clone->data.image.rect.y, 50.0);
  require_close(clone->data.image.rect.width, 140.0);
  require_close(clone->data.image.rect.height, 70.0);
  shaula_annotation_free(clone);
  shaula_annotation_free(image);
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

  require_true(shaula_annotation_resize_image_from_origin(
      image, image->data.image.rect, (ShaulaPoint){0, 0},
      (ShaulaPoint){2, 2}, (ShaulaPoint){3, 3},
      (ShaulaRect){0, 0, 4, 3}, 0.5));
  ShaulaPreviewSnapshot *snapshot =
      shaula_preview_document_snapshot_new(document);
  require_true(snapshot != NULL);
  gdk_pixbuf_get_pixels(image->data.image.pixbuf)[0] = 0x11;
  shaula_preview_document_restore_snapshot(document, snapshot);
  require_true(document->annotations->len == 1);
  ShaulaAnnotation *restored = g_ptr_array_index(document->annotations, 0);
  require_true(restored->type == SHAULA_ANNOTATION_IMAGE);
  require_close(restored->data.image.rect.width, 3.0);
  require_close(restored->data.image.rect.height, 3.0);
  require_true(gdk_pixbuf_get_pixels(restored->data.image.pixbuf)[0] == 0x77);
  shaula_preview_snapshot_free(snapshot);
  require_true(shaula_preview_document_clear_annotations(document));
}

static ShaulaPoint fixed_corner_for_bounds(ShaulaRect bounds,
                                           ShaulaPoint fixed,
                                           ShaulaPoint dragged) {
  return (ShaulaPoint){fixed.x > dragged.x ? bounds.x + bounds.width : bounds.x,
                       fixed.y > dragged.y ? bounds.y + bounds.height
                                           : bounds.y};
}

static void assert_text_resize_contract(void) {
  const ShaulaTextAlign alignments[] = {
      SHAULA_TEXT_ALIGN_LEFT,
      SHAULA_TEXT_ALIGN_CENTER,
      SHAULA_TEXT_ALIGN_RIGHT,
  };
  const ShaulaTextFontMode modes[] = {
      SHAULA_TEXT_FONT_NORMAL,
      SHAULA_TEXT_FONT_SKETCH,
  };

  for (guint mode_index = 0; mode_index < G_N_ELEMENTS(modes); mode_index++) {
    for (guint align_index = 0; align_index < G_N_ELEMENTS(alignments);
         align_index++) {
      ShaulaAnnotation *text = shaula_annotation_new_text(
          (ShaulaPoint){400, 220}, "First line\nSecond line",
          shaula_color_default(), 24.0, alignments[align_index],
          modes[mode_index]);
      require_true(text != NULL);
      ShaulaPoint origin_position = text->data.text.position;
      ShaulaRect origin_bounds = shaula_annotation_selection_bounds(text);
      require_true(isfinite(origin_bounds.x) && isfinite(origin_bounds.y));
      require_true(origin_bounds.width > 0.0 && origin_bounds.height > 0.0);

      ShaulaPoint fixed;
      ShaulaPoint dragged;
      if (alignments[align_index] == SHAULA_TEXT_ALIGN_LEFT) {
        fixed = (ShaulaPoint){origin_bounds.x + origin_bounds.width,
                             origin_bounds.y + origin_bounds.height};
        dragged = (ShaulaPoint){origin_bounds.x, origin_bounds.y};
      } else if (alignments[align_index] == SHAULA_TEXT_ALIGN_CENTER) {
        fixed = (ShaulaPoint){origin_bounds.x, origin_bounds.y};
        dragged = (ShaulaPoint){origin_bounds.x + origin_bounds.width,
                                origin_bounds.y + origin_bounds.height};
      } else {
        fixed = (ShaulaPoint){origin_bounds.x + origin_bounds.width,
                              origin_bounds.y};
        dragged = (ShaulaPoint){origin_bounds.x,
                                origin_bounds.y + origin_bounds.height};
      }
      ShaulaPoint pointer = {
          fixed.x + 1.5 * (dragged.x - fixed.x),
          fixed.y + 1.5 * (dragged.y - fixed.y),
      };

      require_true(shaula_annotation_resize_text_from_origin(
          text, origin_position, 24.0, origin_bounds, fixed, dragged, pointer,
          (ShaulaRect){0, 0, 1600, 1000}));
      require_close(text->data.text.font_size, 36.0);
      require_true(strcmp(text->data.text.text,
                          "First line\nSecond line") == 0);
      require_true(text->data.text.align == alignments[align_index]);
      require_true(text->data.text.font_mode == modes[mode_index]);

      ShaulaRect resized_bounds = shaula_annotation_selection_bounds(text);
      require_true(isfinite(resized_bounds.x) && isfinite(resized_bounds.y));
      require_true(isfinite(resized_bounds.width) &&
                   isfinite(resized_bounds.height));
      require_true(resized_bounds.width > 0.0 && resized_bounds.height > 0.0);
      ShaulaPoint resized_fixed =
          fixed_corner_for_bounds(resized_bounds, fixed, dragged);
      require_close(resized_fixed.x, fixed.x);
      require_close(resized_fixed.y, fixed.y);

      ShaulaAnnotation *clone = shaula_annotation_clone(text);
      require_true(clone != NULL);
      require_close(clone->data.text.font_size, text->data.text.font_size);
      require_close(clone->data.text.position.x, text->data.text.position.x);
      require_close(clone->data.text.position.y, text->data.text.position.y);
      require_true(strcmp(clone->data.text.text, text->data.text.text) == 0);
      shaula_annotation_free(clone);
      shaula_annotation_free(text);
    }
  }
}

static void assert_text_snapshot_contract(void) {
  GdkPixbuf *base =
      gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 800, 600);
  require_true(base != NULL);
  ShaulaPreviewDocument document;
  memset(&document, 0, sizeof(document));
  shaula_preview_document_init(&document, "/tmp/shaula-text-snapshot.png",
                               base);

  ShaulaAnnotation *text = shaula_annotation_new_text(
      (ShaulaPoint){400, 180}, "Snapshot\ntext", shaula_color_default(), 24.0,
      SHAULA_TEXT_ALIGN_CENTER, SHAULA_TEXT_FONT_SKETCH);
  require_true(text != NULL);
  ShaulaRect origin_bounds = shaula_annotation_selection_bounds(text);
  ShaulaPoint fixed = {origin_bounds.x, origin_bounds.y};
  ShaulaPoint dragged = {origin_bounds.x + origin_bounds.width,
                         origin_bounds.y + origin_bounds.height};
  ShaulaPoint pointer = {fixed.x + 1.5 * (dragged.x - fixed.x),
                         fixed.y + 1.5 * (dragged.y - fixed.y)};
  require_true(shaula_annotation_resize_text_from_origin(
      text, text->data.text.position, text->data.text.font_size, origin_bounds,
      fixed, dragged, pointer, (ShaulaRect){0, 0, 800, 600}));
  double resized_font_size = text->data.text.font_size;
  ShaulaPoint resized_position = text->data.text.position;
  shaula_preview_document_add_annotation(&document, text);

  ShaulaPreviewSnapshot *snapshot =
      shaula_preview_document_snapshot_new(&document);
  require_true(snapshot != NULL);
  text->data.text.font_size = SHAULA_TEXT_FONT_SIZE_MIN;
  text->data.text.position = (ShaulaPoint){0, 0};
  shaula_annotation_update_bounds(text);
  shaula_preview_document_restore_snapshot(&document, snapshot);
  require_true(document.annotations->len == 1);
  ShaulaAnnotation *restored = g_ptr_array_index(document.annotations, 0);
  require_true(restored->type == SHAULA_ANNOTATION_TEXT);
  require_close(restored->data.text.font_size, resized_font_size);
  require_close(restored->data.text.position.x, resized_position.x);
  require_close(restored->data.text.position.y, resized_position.y);
  require_true(strcmp(restored->data.text.text, "Snapshot\ntext") == 0);

  shaula_preview_snapshot_free(snapshot);
  shaula_preview_document_free(&document);
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
  assert_image_resize_contract();
  assert_image_annotation_contract();
  assert_image_snapshot_contract(&document);
  assert_text_resize_contract();
  assert_text_snapshot_contract();
  assert_selected_group_bounds();

  shaula_preview_document_free(&document);
  return 0;
}
