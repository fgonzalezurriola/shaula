#ifndef SHAULA_RUNTIME_SAVE_DESTINATION_H
#define SHAULA_RUNTIME_SAVE_DESTINATION_H

#include <glib.h>

G_BEGIN_DECLS

#define SHAULA_SAVE_DEFAULT_FOLDER "~/Pictures/shaula"
#define SHAULA_SAVE_DEFAULT_TEMPLATE "{date}-{time}"

/* Save destinations are a runtime boundary shared by capture, Preview, and
 * directory commands. Templates are filename stems; `.png` is appended by the
 * resolver and path separators are never accepted.
 */
gboolean shaula_save_folder_validate(const char *folder);
gboolean shaula_save_template_validate(const char *filename_template);
char *shaula_save_directory_resolve(const char *folder, gboolean create,
                                    GError **error);
char *shaula_save_basename_new(const char *filename_template, GDateTime *now);
char *shaula_save_unique_path_new(const char *directory,
                                  const char *filename_template,
                                  GDateTime *now, GError **error);

G_END_DECLS

#endif
