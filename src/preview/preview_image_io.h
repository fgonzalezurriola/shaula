#ifndef SHAULA_PREVIEW_IMAGE_IO_H
#define SHAULA_PREVIEW_IMAGE_IO_H

#include <glib.h>

/*
 * Copies arbitrary bytes between borrowed paths. On GLib I/O failure, returns
 * FALSE and may set error; missing path arguments return FALSE without an
 * error.
 */
gboolean
shaula_image_io_copy_file_bytes(const char *source, const char *target,
                                GError **error) G_GNUC_WARN_UNUSED_RESULT;

/* Returns whether the borrowed path ends in .png, ignoring ASCII case. */
gboolean shaula_image_io_path_has_png_extension(const char *path);

/*
 * Returns a new GLib-owned path that the caller must release with g_free().
 * NULL is returned only when the borrowed input path is NULL.
 */
char *
shaula_image_io_with_png_extension(const char *path) G_GNUC_WARN_UNUSED_RESULT;

/*
 * Launches xdg-open asynchronously with the containing directory as one argv
 * element. The borrowed path is never interpreted by a shell.
 */
gboolean shaula_image_io_open_containing_folder(
    const char *path, GError **error) G_GNUC_WARN_UNUSED_RESULT;

#endif
