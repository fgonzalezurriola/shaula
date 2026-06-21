#ifndef SHAULA_PREVIEW_ANNOTATION_EDITOR_H
#define SHAULA_PREVIEW_ANNOTATION_EDITOR_H

#include <glib.h>

#include "preview_annotations.h"
#include "preview_geometry.h"

typedef struct ShaulaPreviewState ShaulaPreviewState;

typedef struct {
  GArray *selected_ids;
} ShaulaAnnotationEditor;

void shaula_annotation_editor_init(ShaulaAnnotationEditor *editor);
void shaula_annotation_editor_dispose(ShaulaAnnotationEditor *editor);

/* Rebuilds the editor selection from document flags after snapshot or document
 * replacement. This is the only supported ingress for persisted selection.
 */
void shaula_annotation_editor_rebuild_selection(ShaulaPreviewState *state);

gboolean shaula_annotation_editor_has_selection(
    const ShaulaPreviewState *state);
guint shaula_annotation_editor_selected_count(
    const ShaulaPreviewState *state);
ShaulaAnnotation *shaula_annotation_editor_single_selection(
    const ShaulaPreviewState *state);
gboolean shaula_annotation_editor_is_selected(
    const ShaulaPreviewState *state, const ShaulaAnnotation *annotation);
gboolean shaula_annotation_editor_selected_bounds(
    const ShaulaPreviewState *state, ShaulaRect *bounds_out);

void shaula_annotation_editor_select_only(ShaulaPreviewState *state,
                                          ShaulaAnnotation *annotation);
void shaula_annotation_editor_toggle_selection(ShaulaPreviewState *state,
                                               ShaulaAnnotation *annotation);
void shaula_annotation_editor_clear_selection(ShaulaPreviewState *state);
void shaula_annotation_editor_select_all(ShaulaPreviewState *state);
guint shaula_annotation_editor_select_intersecting_rect(
    ShaulaPreviewState *state, ShaulaRect rect);

/* Annotation mutations cross one seam so selection, history, clipboard, HUD,
 * redraw, and toolbar state remain one transaction.
 */
void shaula_annotation_editor_add_annotation(ShaulaPreviewState *state,
                                             ShaulaAnnotation *annotation);
void shaula_annotation_editor_move_selected(ShaulaPreviewState *state,
                                            double dx, double dy);
gboolean shaula_annotation_editor_can_paste(const ShaulaPreviewState *state);
gboolean shaula_annotation_editor_duplicate_selected(ShaulaPreviewState *state);
gboolean shaula_annotation_editor_copy_selected(ShaulaPreviewState *state);
gboolean shaula_annotation_editor_cut_selected(ShaulaPreviewState *state);
gboolean shaula_annotation_editor_paste(ShaulaPreviewState *state);
void shaula_annotation_editor_delete_selected(ShaulaPreviewState *state);
void shaula_annotation_editor_reset_annotations(ShaulaPreviewState *state);

#endif
