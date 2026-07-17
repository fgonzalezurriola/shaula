#ifndef SHAULA_OVERLAY_PROTOCOL_H
#define SHAULA_OVERLAY_PROTOCOL_H

#include "overlay_selection.h"

#include <glib.h>

typedef enum {
  SHAULA_OVERLAY_INTERACTION_QUICK,
  SHAULA_OVERLAY_INTERACTION_AREA,
} ShaulaOverlayInteraction;

typedef struct {
  ShaulaOverlayInteraction interaction;
  gboolean has_aspect;
  int aspect_width;
  int aspect_height;
  char *background_path;
  char *output_name;
  gboolean has_initial_geometry;
  gboolean initial_geometry_legacy;
  ShaulaRect initial_geometry;
} ShaulaOverlayLaunch;

typedef enum {
  SHAULA_OVERLAY_OUTCOME_INVALID,
  SHAULA_OVERLAY_OUTCOME_OK,
  SHAULA_OVERLAY_OUTCOME_CANCEL,
  SHAULA_OVERLAY_OUTCOME_ERROR,
} ShaulaOverlayOutcomeStatus;

typedef enum {
  SHAULA_OVERLAY_ACTION_CAPTURE,
  SHAULA_OVERLAY_ACTION_COPY,
  SHAULA_OVERLAY_ACTION_SAVE,
  SHAULA_OVERLAY_ACTION_CANCEL,
} ShaulaOverlayAction;

typedef struct {
  ShaulaOverlayOutcomeStatus status;
  ShaulaOverlayAction action;
  gboolean has_geometry;
  ShaulaRect geometry;
  gboolean has_local_geometry;
  ShaulaRect local_geometry;
  gboolean has_output;
  ShaulaRect output_geometry;
  char *output_name;
  char *aspect;
  char *error_code;
  char *error_message;
} ShaulaOverlayOutcome;

/* Loads and owns the complete helper launch contract from the process
 * environment. Invalid optional aspect/geometry values remain absent, matching
 * the interactive fallback contract.
 */
void shaula_overlay_launch_init(ShaulaOverlayLaunch *launch);
void shaula_overlay_launch_clear(ShaulaOverlayLaunch *launch);
void shaula_overlay_launch_load_environment(ShaulaOverlayLaunch *launch);

/* Applies the helper launch contract to an owned GLib environment vector.
 * Ownership of environment transfers into the function and back to the caller.
 */
char **shaula_overlay_launch_environment_apply(
    char **environment, ShaulaOverlayInteraction interaction,
    const char *aspect, gboolean frozen, const char *background_path,
    const char *output_name);

void shaula_overlay_outcome_init(ShaulaOverlayOutcome *outcome);
void shaula_overlay_outcome_clear(ShaulaOverlayOutcome *outcome);
void shaula_overlay_outcome_set_success(
    ShaulaOverlayOutcome *outcome, ShaulaOverlayAction action,
    const char *aspect, ShaulaRect geometry, ShaulaRect local_geometry,
    ShaulaRect output_geometry, const char *output_name);
void shaula_overlay_outcome_set_cancel(ShaulaOverlayOutcome *outcome,
                                       const ShaulaRect *geometry);
void shaula_overlay_outcome_set_error(ShaulaOverlayOutcome *outcome,
                                      const char *code, const char *message);

/* Returned JSON is GLib-owned. Parsing rejects malformed status/action/geometry
 * combinations so Capture can map them deterministically to
 * ERR_OVERLAY_PROTOCOL_INVALID.
 */
char *shaula_overlay_outcome_json_new(const ShaulaOverlayOutcome *outcome);
gboolean shaula_overlay_outcome_parse(const char *json,
                                      ShaulaOverlayOutcome *outcome);

const char *shaula_overlay_action_text(ShaulaOverlayAction action);

#endif
