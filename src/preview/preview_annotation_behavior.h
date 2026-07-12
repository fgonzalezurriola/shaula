#ifndef SHAULA_PREVIEW_ANNOTATION_BEHAVIOR_H
#define SHAULA_PREVIEW_ANNOTATION_BEHAVIOR_H

#include "preview_annotations.h"

typedef enum {
  SHAULA_ANNOTATION_QUERY_SELECTION,
  SHAULA_ANNOTATION_QUERY_ERASER,
} ShaulaAnnotationQueryKind;

typedef struct {
  ShaulaAnnotationQueryKind kind;
  ShaulaRect rect;
  ShaulaPoint start;
  ShaulaPoint end;
  double tolerance;
} ShaulaAnnotationQuery;

/* This is the Preview's annotation behavior seam: callers express an editing
 * intent without branching on annotation variants or representation details. */
ShaulaAnnotationHit shaula_annotation_behavior_hit_test(
    GPtrArray *annotations, ShaulaPoint point, double tolerance);
gboolean shaula_annotation_behavior_matches(
    const ShaulaAnnotation *annotation, ShaulaAnnotationQuery query);

#endif
