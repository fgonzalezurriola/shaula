#ifndef SHAULA_PREVIEW_IMAGE_IO_H
#define SHAULA_PREVIEW_IMAGE_IO_H

#include <glib.h>

gboolean shaula_image_io_copy_file_bytes(const char *source,
                                         const char *target, GError **error);
gboolean shaula_image_io_path_has_png_extension(const char *path);
char *shaula_image_io_with_png_extension(const char *path);
gboolean shaula_image_io_open_containing_folder(const char *path,
                                                GError **error);

#endif
