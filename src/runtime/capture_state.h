#ifndef SHAULA_RUNTIME_CAPTURE_STATE_H
#define SHAULA_RUNTIME_CAPTURE_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SHAULA_CAPTURE_STATE_STATUS_OK = 0,
  SHAULA_CAPTURE_STATE_STATUS_BUSY = 1,
  SHAULA_CAPTURE_STATE_STATUS_INVALID_ARGUMENT = 2,
  SHAULA_CAPTURE_STATE_STATUS_OUT_OF_MEMORY = 3,
  SHAULA_CAPTURE_STATE_STATUS_FILESYSTEM_ERROR = 4,
} ShaulaCaptureStateStatus;

typedef struct ShaulaCaptureStateSession ShaulaCaptureStateSession;

typedef struct {
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
} ShaulaCaptureStateGeometry;

/*
 * Capture runtime state owns every capture-specific runtime location and its
 * persistence mechanics. The capture lifecycle owns sequencing only, including
 * the requirement to release the session before launching Preview.
 *
 * Returned paths are GLib-owned strings released with g_free(). Previous-area
 * reads preserve fail-closed behavior: malformed or unreadable state returns OK
 * with *out_present set to zero.
 */
ShaulaCaptureStateStatus
shaula_capture_state_session_acquire(ShaulaCaptureStateSession **out_session);

void shaula_capture_state_session_release(ShaulaCaptureStateSession *session);

char *shaula_capture_state_capture_directory(void);
char *shaula_capture_state_overlay_background_path(int64_t identity);

ShaulaCaptureStateStatus
shaula_capture_state_previous_load(int32_t *out_present,
                                   ShaulaCaptureStateGeometry *out_geometry);

ShaulaCaptureStateStatus
shaula_capture_state_previous_store(ShaulaCaptureStateGeometry geometry);

#ifdef __cplusplus
}
#endif

#endif
