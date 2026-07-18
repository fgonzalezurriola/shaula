#ifndef SHAULA_CONFIG_NOCTALIA_MANAGED_H
#define SHAULA_CONFIG_NOCTALIA_MANAGED_H

#include <glib.h>
#include <stdint.h>

typedef int32_t ShaulaNoctaliaStatus;
enum {
  SHAULA_NOCTALIA_STATUS_OK = 0,
  SHAULA_NOCTALIA_STATUS_NOT_DETECTED = 1,
  SHAULA_NOCTALIA_STATUS_SOURCE_UNAVAILABLE = 2,
  SHAULA_NOCTALIA_STATUS_INVALID_STATE = 3,
  SHAULA_NOCTALIA_STATUS_UNMANAGED_PLUGIN = 4,
  SHAULA_NOCTALIA_STATUS_IO_FAILED = 5,
};

typedef struct {
  gboolean detected;
  gboolean changed;
  gboolean plugin_files_changed;
  gboolean plugins_json_changed;
  gboolean settings_json_changed;
  gboolean plugins_json_skipped;
  gboolean settings_json_skipped;
  char *plugin_dir;
} ShaulaNoctaliaResult;

void shaula_noctalia_result_clear(ShaulaNoctaliaResult *result);

/* Resolve the distributed immutable plugin payload. The returned path is owned. */
char *shaula_noctalia_plugin_source_resolve(void);

/* Detect user state under XDG_CONFIG_HOME, falling back to ~/.config only when
 * XDG_CONFIG_HOME is unset. */
gboolean shaula_noctalia_detected(void);

/*
 * Install/remove the Shaula-owned plugin directory and matching Noctalia JSON
 * state. Existing JSON is validated before mutation; changed files are backed
 * up and atomically replaced. An unmarked plugin directory is never overwritten
 * or removed.
 */
ShaulaNoctaliaStatus
shaula_noctalia_install(const char *source_dir, gboolean dry_run,
                         ShaulaNoctaliaResult *result);
ShaulaNoctaliaStatus shaula_noctalia_remove(gboolean dry_run,
                                            ShaulaNoctaliaResult *result);

const char *shaula_noctalia_status_token(ShaulaNoctaliaStatus status);

#endif
