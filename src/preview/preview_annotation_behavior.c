#include "preview_annotation_behavior.h"

ShaulaAnnotationHit shaula_annotation_behavior_hit_test(
    GPtrArray *annotations, ShaulaPoint point, double tolerance) {
  return shaula_annotations_hit_test_ranked(annotations, point, tolerance);
}

gboolean shaula_annotation_behavior_matches(
    const ShaulaAnnotation *annotation, ShaulaAnnotationQuery query) {
  switch (query.kind) {
  case SHAULA_ANNOTATION_QUERY_SELECTION:
    return shaula_annotation_intersects_selection_rect(annotation, query.rect);
  case SHAULA_ANNOTATION_QUERY_ERASER:
    return shaula_annotation_intersects_eraser_segment(
        annotation, query.start, query.end, query.tolerance);
  }
  return FALSE;
}
