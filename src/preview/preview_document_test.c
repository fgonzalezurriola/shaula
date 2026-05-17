#include "preview_document.h"

#include <assert.h>
#include <string.h>

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

  shaula_preview_document_free(&document);
  return 0;
}
