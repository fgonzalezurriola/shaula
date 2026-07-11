#ifndef SHAULA_CONFIG_CONFIG_H
#define SHAULA_CONFIG_CONFIG_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  SHAULA_CONFIG_STATUS_OK = 0,
  SHAULA_CONFIG_STATUS_UNREADABLE,
  SHAULA_CONFIG_STATUS_INVALID,
} ShaulaConfigStatus;

typedef struct {
  gboolean skip_preview;
  gboolean copy_to_clipboard;
  gboolean save_to_folder;
} ShaulaAfterModeConfig;

typedef struct {
  char region_capture_mode[16];
  char save_folder[4096];
  ShaulaAfterModeConfig quick;
  ShaulaAfterModeConfig area;
  ShaulaAfterModeConfig fullscreen;
  ShaulaAfterModeConfig all_screens;
  gboolean notifications_success;
  gboolean notifications_errors;
  gboolean notifications_thumbnails;
  char preview_mode[32];
  gboolean preview_focused;
  gboolean close_preview_on_save;
  guint32 preview_width;
  guint32 preview_height;
  char column_display[16];
  gboolean floating_x_set;
  gboolean floating_y_set;
  gint32 floating_x;
  gint32 floating_y;
  char floating_relative_to[24];
} ShaulaConfig;

/* Initializes the integrated product defaults used when no config exists. */
void shaula_config_init_defaults(ShaulaConfig *config);

/* Returns a newly allocated path, or NULL when no config root is available. */
char *shaula_config_path_new(void);

/*
 * Loads the strict public TOML subset. Missing files are successful defaults
 * with loaded set to FALSE. Unknown sections/keys and invalid values produce
 * SHAULA_CONFIG_STATUS_INVALID so callers can preserve ERR_CONFIG_INVALID.
 */
ShaulaConfigStatus shaula_config_load(const char *path, ShaulaConfig *config,
                                      gboolean *loaded);

/* Returns newly allocated canonical TOML for the complete public config. */
char *shaula_config_serialize(const ShaulaConfig *config);

/* Atomically replaces path, creating its parent and a best-effort .bak copy. */
ShaulaConfigStatus shaula_config_save(const char *path,
                                      const ShaulaConfig *config);

gboolean shaula_config_validate(const ShaulaConfig *config);

G_END_DECLS

#endif
