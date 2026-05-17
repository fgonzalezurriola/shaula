#ifndef SHAULA_PREVIEW_DOCUMENT_H
#define SHAULA_PREVIEW_DOCUMENT_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#include "preview_annotations.h"
#include "preview_geometry.h"
#include "preview_properties_hud.h"

typedef struct {
  /* Document effect entry: copied/saved output must use these stored values,
   * not the current Spotlight toolbar settings.
   */
  ShaulaRect rect;
  ShaulaSpotlightShape shape;
  ShaulaColor border_color;
  double border_width;
} ShaulaSpotlightRegion;

typedef struct ShaulaPreviewSnapshot ShaulaPreviewSnapshot;

typedef struct {
  GPtrArray *undo;
  GPtrArray *redo;
  guint capacity;
} ShaulaHistoryStack;

typedef struct {
  GPtrArray *annotations;
  int last_pasted_id;
} ShaulaAnnotationClipboard;

/* Output-affecting preview document state. View/tool/HUD state stays in
 * ShaulaPreviewState and must not be captured in document undo snapshots.
 */
typedef struct {
  GdkPixbuf *image;
  char *path;
  GPtrArray *annotations;
  ShaulaAnnotationClipboard annotation_clipboard;
  GArray *spotlight_regions;
  int next_annotation_id;
  ShaulaHistoryStack history;
  ShaulaPreviewSnapshot *pending_history_snapshot;
  gboolean modified;
  gboolean copied;
  gboolean saved;
  char *saved_path;
} ShaulaPreviewDocument;

void shaula_preview_document_init(ShaulaPreviewDocument *document,
                                  const char *path, GdkPixbuf *image);
void shaula_preview_document_free(ShaulaPreviewDocument *document);
gboolean
shaula_preview_document_has_modifications(ShaulaPreviewDocument *document);
int shaula_preview_document_width(ShaulaPreviewDocument *document);
int shaula_preview_document_height(ShaulaPreviewDocument *document);

ShaulaPreviewSnapshot *
shaula_preview_document_snapshot_new(ShaulaPreviewDocument *document);
void shaula_preview_snapshot_free(gpointer data);
void shaula_preview_document_restore_snapshot(
    ShaulaPreviewDocument *document, ShaulaPreviewSnapshot *snapshot,
    ShaulaAnnotation **selected_annotation);

void shaula_preview_history_push_undo(ShaulaHistoryStack *history,
                                      ShaulaPreviewSnapshot *snapshot,
                                      gboolean clear_redo);
ShaulaPreviewSnapshot *
shaula_preview_history_pop_undo(ShaulaHistoryStack *history);
ShaulaPreviewSnapshot *
shaula_preview_history_pop_redo(ShaulaHistoryStack *history);
void shaula_preview_history_push_redo(ShaulaHistoryStack *history,
                                      ShaulaPreviewSnapshot *snapshot);
void shaula_preview_history_clear_redo(ShaulaHistoryStack *history);
gboolean shaula_preview_history_can_undo(ShaulaHistoryStack *history);
gboolean shaula_preview_history_can_redo(ShaulaHistoryStack *history);

void shaula_preview_annotation_clipboard_clear(
    ShaulaAnnotationClipboard *clipboard);

#endif
