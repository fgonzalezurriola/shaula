#ifndef SHAULA_CONFIG_NIRI_MANAGED_H
#define SHAULA_CONFIG_NIRI_MANAGED_H

#include <glib.h>

typedef struct {
  char *path;
  char *backup_path;
  gboolean installed;
  gboolean replaced;
  gboolean changed;
} ManagedBlockResult;

void managed_block_result_clear(ManagedBlockResult *result);
char *niri_config_path(const char *override);
gboolean install_managed_block(const char *path_override,
                               const char *begin_marker,
                               const char *end_marker,
                               const char *payload, gboolean dry_run,
                               ManagedBlockResult *result, gboolean *invalid);
GPtrArray *niri_keybind_conflicts(const char *path);
gboolean remove_managed_keybinds(const char *path_override, gboolean dry_run,
                                 ManagedBlockResult *result,
                                 gboolean *invalid);

#endif

