#include "capture_state.h"

#include "capture_session_lock.h"
#include "paths.h"
#include "previous_area_store.h"

#include <glib.h>
#include <string.h>

struct ShaulaCaptureStateSession {
  char *path;
};

static ShaulaCaptureStateStatus map_path_status(ShaulaRuntimePathStatus status) {
  switch (status) {
  case SHAULA_RUNTIME_PATH_STATUS_OK:
    return SHAULA_CAPTURE_STATE_STATUS_OK;
  case SHAULA_RUNTIME_PATH_STATUS_INVALID_ARGUMENT:
    return SHAULA_CAPTURE_STATE_STATUS_INVALID_ARGUMENT;
  case SHAULA_RUNTIME_PATH_STATUS_OUT_OF_MEMORY:
    return SHAULA_CAPTURE_STATE_STATUS_OUT_OF_MEMORY;
  case SHAULA_RUNTIME_PATH_STATUS_FILESYSTEM_ERROR:
  default:
    return SHAULA_CAPTURE_STATE_STATUS_FILESYSTEM_ERROR;
  }
}

static ShaulaCaptureStateStatus
map_session_status(ShaulaCaptureSessionStatus status) {
  switch (status) {
  case SHAULA_CAPTURE_SESSION_STATUS_OK:
    return SHAULA_CAPTURE_STATE_STATUS_OK;
  case SHAULA_CAPTURE_SESSION_STATUS_BUSY:
    return SHAULA_CAPTURE_STATE_STATUS_BUSY;
  case SHAULA_CAPTURE_SESSION_STATUS_INVALID_ARGUMENT:
    return SHAULA_CAPTURE_STATE_STATUS_INVALID_ARGUMENT;
  case SHAULA_CAPTURE_SESSION_STATUS_OUT_OF_MEMORY:
    return SHAULA_CAPTURE_STATE_STATUS_OUT_OF_MEMORY;
  case SHAULA_CAPTURE_SESSION_STATUS_FILESYSTEM_ERROR:
  default:
    return SHAULA_CAPTURE_STATE_STATUS_FILESYSTEM_ERROR;
  }
}

static ShaulaCaptureStateStatus
map_previous_status(ShaulaPreviousAreaStatus status) {
  switch (status) {
  case SHAULA_PREVIOUS_AREA_STATUS_OK:
    return SHAULA_CAPTURE_STATE_STATUS_OK;
  case SHAULA_PREVIOUS_AREA_STATUS_INVALID_ARGUMENT:
    return SHAULA_CAPTURE_STATE_STATUS_INVALID_ARGUMENT;
  case SHAULA_PREVIOUS_AREA_STATUS_OUT_OF_MEMORY:
    return SHAULA_CAPTURE_STATE_STATUS_OUT_OF_MEMORY;
  case SHAULA_PREVIOUS_AREA_STATUS_FILESYSTEM_ERROR:
  default:
    return SHAULA_CAPTURE_STATE_STATUS_FILESYSTEM_ERROR;
  }
}

static ShaulaCaptureStateStatus resolve_state_path(const char *relative,
                                                   char **out_path) {
  ShaulaRuntimeOwnedPath path = {0};
  ShaulaRuntimePathStatus status;

  if (out_path == NULL || relative == NULL || relative[0] == '\0') {
    return SHAULA_CAPTURE_STATE_STATUS_INVALID_ARGUMENT;
  }
  *out_path = NULL;
  status = shaula_runtime_path_resolve(
      NULL, g_getenv("XDG_RUNTIME_DIR"),
      (ShaulaRuntimePathSpan){.data = relative, .length = strlen(relative)},
      &path);
  if (status != SHAULA_RUNTIME_PATH_STATUS_OK) {
    return map_path_status(status);
  }

  *out_path = path.data;
  return SHAULA_CAPTURE_STATE_STATUS_OK;
}

ShaulaCaptureStateStatus
shaula_capture_state_session_acquire(ShaulaCaptureStateSession **out_session) {
  g_autofree char *path = NULL;
  ShaulaCaptureStateSession *session;
  ShaulaCaptureStateStatus status;

  if (out_session == NULL) {
    return SHAULA_CAPTURE_STATE_STATUS_INVALID_ARGUMENT;
  }
  *out_session = NULL;

  status = resolve_state_path("capture.lock", &path);
  if (status != SHAULA_CAPTURE_STATE_STATUS_OK) {
    return status;
  }
  status = map_session_status(shaula_capture_session_lock_acquire(
      (ShaulaCaptureSessionSpan){.data = path, .length = strlen(path)}));
  if (status != SHAULA_CAPTURE_STATE_STATUS_OK) {
    return status;
  }

  session = g_try_new0(ShaulaCaptureStateSession, 1);
  if (session == NULL) {
    shaula_capture_session_lock_release(
        (ShaulaCaptureSessionSpan){.data = path, .length = strlen(path)});
    return SHAULA_CAPTURE_STATE_STATUS_OUT_OF_MEMORY;
  }
  session->path = g_steal_pointer(&path);
  *out_session = session;
  return SHAULA_CAPTURE_STATE_STATUS_OK;
}

void shaula_capture_state_session_release(ShaulaCaptureStateSession *session) {
  if (session == NULL) {
    return;
  }
  if (session->path != NULL) {
    shaula_capture_session_lock_release((ShaulaCaptureSessionSpan){
        .data = session->path, .length = strlen(session->path)});
  }
  g_free(session->path);
  g_free(session);
}

char *shaula_capture_state_capture_directory(void) {
  char *directory = NULL;

  if (resolve_state_path("captures", &directory) !=
      SHAULA_CAPTURE_STATE_STATUS_OK) {
    return NULL;
  }
  if (g_mkdir_with_parents(directory, 0755) != 0) {
    g_free(directory);
    return NULL;
  }
  return directory;
}

char *shaula_capture_state_overlay_background_path(int64_t identity) {
  g_autofree char *directory = NULL;

  if (resolve_state_path("overlay", &directory) !=
      SHAULA_CAPTURE_STATE_STATUS_OK) {
    return NULL;
  }
  if (g_mkdir_with_parents(directory, 0700) != 0) {
    return NULL;
  }
  return g_strdup_printf("%s/background-%" G_GINT64_FORMAT ".png", directory,
                         (gint64)identity);
}

ShaulaCaptureStateStatus
shaula_capture_state_previous_load(int32_t *out_present,
                                   ShaulaCaptureStateGeometry *out_geometry) {
  g_autofree char *path = NULL;
  ShaulaPreviousAreaGeometry geometry = {0};
  ShaulaCaptureStateStatus status;

  if (out_present == NULL || out_geometry == NULL) {
    return SHAULA_CAPTURE_STATE_STATUS_INVALID_ARGUMENT;
  }
  *out_present = 0;
  *out_geometry = (ShaulaCaptureStateGeometry){0};

  status = resolve_state_path("previous-area.v1", &path);
  if (status != SHAULA_CAPTURE_STATE_STATUS_OK) {
    return status;
  }
  status = map_previous_status(shaula_previous_area_load(
      (ShaulaPreviousAreaSpan){.data = path, .length = strlen(path)},
      out_present, &geometry));
  if (status == SHAULA_CAPTURE_STATE_STATUS_OK && *out_present != 0) {
    *out_geometry = (ShaulaCaptureStateGeometry){
        .x = geometry.x,
        .y = geometry.y,
        .width = geometry.width,
        .height = geometry.height,
    };
  }
  return status;
}

ShaulaCaptureStateStatus
shaula_capture_state_previous_store(ShaulaCaptureStateGeometry geometry) {
  g_autofree char *path = NULL;
  ShaulaCaptureStateStatus status;

  status = resolve_state_path("previous-area.v1", &path);
  if (status != SHAULA_CAPTURE_STATE_STATUS_OK) {
    return status;
  }
  return map_previous_status(shaula_previous_area_store(
      (ShaulaPreviousAreaSpan){.data = path, .length = strlen(path)},
      (ShaulaPreviousAreaGeometry){
          .x = geometry.x,
          .y = geometry.y,
          .width = geometry.width,
          .height = geometry.height,
      }));
}
