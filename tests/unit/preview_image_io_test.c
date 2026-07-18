#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "preview_image_io.h"

static void test_png_extension_detection(void) {
  g_assert_false(shaula_image_io_path_has_png_extension(NULL));
  g_assert_false(shaula_image_io_path_has_png_extension(""));
  g_assert_false(shaula_image_io_path_has_png_extension("capture"));
  g_assert_false(shaula_image_io_path_has_png_extension("capture.png.tmp"));
  g_assert_false(shaula_image_io_path_has_png_extension("capture."));
  g_assert_true(shaula_image_io_path_has_png_extension("capture.png"));
  g_assert_true(shaula_image_io_path_has_png_extension("capture.PNG"));
  g_assert_true(shaula_image_io_path_has_png_extension(".png"));
}

static void test_png_extension_allocation(void) {
  g_assert_null(shaula_image_io_with_png_extension(NULL));

  g_autofree char *existing = shaula_image_io_with_png_extension("capture.PNG");
  g_assert_cmpstr(existing, ==, "capture.PNG");

  g_autofree char *appended = shaula_image_io_with_png_extension("capture");
  g_assert_cmpstr(appended, ==, "capture.png");

  g_autofree char *empty = shaula_image_io_with_png_extension("");
  g_assert_cmpstr(empty, ==, ".png");
}

static void test_copy_file_bytes(void) {
  GError *error = NULL;
  g_autofree char *tmp_dir = g_dir_make_tmp("shaula-image-io-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(tmp_dir);

  g_autofree char *source = g_build_filename(tmp_dir, "source.bin", NULL);
  g_autofree char *target = g_build_filename(tmp_dir, "target.bin", NULL);
  const guint8 expected[] = {0x00, 0x01, 0xff, 'P', 'N', 'G'};
  g_assert_true(g_file_set_contents(source, (const char *)expected,
                                    (gssize)sizeof expected, &error));
  g_assert_no_error(error);

  g_assert_true(shaula_image_io_copy_file_bytes(source, target, &error));
  g_assert_no_error(error);

  g_autofree char *actual = NULL;
  gsize actual_length = 0;
  g_assert_true(g_file_get_contents(target, &actual, &actual_length, &error));
  g_assert_no_error(error);
  g_assert_cmpuint(actual_length, ==, sizeof expected);
  g_assert_cmpmem(actual, actual_length, expected, sizeof expected);

  g_assert_cmpint(g_remove(target), ==, 0);
  g_assert_cmpint(g_remove(source), ==, 0);
  g_assert_cmpint(g_rmdir(tmp_dir), ==, 0);
}

static void test_copy_file_bytes_errors(void) {
  GError *error = NULL;
  g_assert_false(shaula_image_io_copy_file_bytes(NULL, "target", &error));
  g_assert_no_error(error);
  g_assert_false(shaula_image_io_copy_file_bytes("source", NULL, &error));
  g_assert_no_error(error);

  g_autofree char *tmp_dir = g_dir_make_tmp("shaula-image-io-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(tmp_dir);

  g_autofree char *missing = g_build_filename(tmp_dir, "missing.bin", NULL);
  g_autofree char *target = g_build_filename(tmp_dir, "target.bin", NULL);
  g_assert_false(shaula_image_io_copy_file_bytes(missing, target, &error));
  g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
  g_clear_error(&error);

  g_assert_cmpint(g_rmdir(tmp_dir), ==, 0);
}

static void restore_environment(const char *name, const char *value) {
  if (value != NULL)
    g_setenv(name, value, TRUE);
  else
    g_unsetenv(name);
}

static void test_open_containing_folder_rejects_missing_path(void) {
  GError *error = NULL;
  g_assert_false(shaula_image_io_open_containing_folder(NULL, &error));
  g_assert_no_error(error);
  g_assert_false(shaula_image_io_open_containing_folder("", &error));
  g_assert_no_error(error);
}

static void test_open_containing_folder_uses_exact_argv(void) {
  GError *error = NULL;
  g_autofree char *tmp_dir = g_dir_make_tmp("shaula-image-io-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(tmp_dir);

  g_autofree char *launcher = g_build_filename(tmp_dir, "xdg-open", NULL);
  g_autofree char *log_path = g_build_filename(tmp_dir, "opened-path", NULL);
  g_autofree char *folder =
      g_build_filename(tmp_dir, "folder with spaces;printf injected", NULL);
  g_autofree char *image_path = g_build_filename(folder, "capture.png", NULL);
  g_autofree char *injected_path = g_build_filename(tmp_dir, "injected", NULL);
  const char launcher_script[] =
      "#!/bin/sh\n"
      "printf '%s' \"$1\" > \"$SHAULA_TEST_XDG_OPEN_LOG\"\n";

  g_assert_cmpint(g_mkdir(folder, 0700), ==, 0);
  g_assert_true(g_file_set_contents(launcher, launcher_script, -1, &error));
  g_assert_no_error(error);
  g_assert_cmpint(g_chmod(launcher, 0700), ==, 0);

  g_autofree char *old_path = g_strdup(g_getenv("PATH"));
  g_autofree char *old_log = g_strdup(g_getenv("SHAULA_TEST_XDG_OPEN_LOG"));
  g_setenv("PATH", tmp_dir, TRUE);
  g_setenv("SHAULA_TEST_XDG_OPEN_LOG", log_path, TRUE);

  g_assert_true(shaula_image_io_open_containing_folder(image_path, &error));
  g_assert_no_error(error);

  g_autofree char *opened_path = NULL;
  gsize opened_length = 0;
  for (int attempt = 0; attempt < 1000; attempt += 1) {
    if (g_file_test(log_path, G_FILE_TEST_EXISTS)) {
      GError *read_error = NULL;
      g_clear_pointer(&opened_path, g_free);
      opened_length = 0;
      if (!g_file_get_contents(log_path, &opened_path, &opened_length,
                               &read_error))
        g_clear_error(&read_error);
      if (opened_path != NULL && opened_length == strlen(folder))
        break;
    }
    g_usleep(5000);
  }

  restore_environment("PATH", old_path);
  restore_environment("SHAULA_TEST_XDG_OPEN_LOG", old_log);

  g_assert_nonnull(opened_path);
  g_assert_cmpuint(opened_length, ==, strlen(folder));
  g_assert_cmpmem(opened_path, opened_length, folder, strlen(folder));
  g_assert_false(g_file_test(injected_path, G_FILE_TEST_EXISTS));

  g_assert_cmpint(g_remove(log_path), ==, 0);
  g_assert_cmpint(g_remove(launcher), ==, 0);
  g_assert_cmpint(g_rmdir(folder), ==, 0);
  g_assert_cmpint(g_rmdir(tmp_dir), ==, 0);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/preview/image-io/png-extension-detection",
                  test_png_extension_detection);
  g_test_add_func("/preview/image-io/png-extension-allocation",
                  test_png_extension_allocation);
  g_test_add_func("/preview/image-io/copy-file-bytes", test_copy_file_bytes);
  g_test_add_func("/preview/image-io/copy-file-bytes-errors",
                  test_copy_file_bytes_errors);
  g_test_add_func("/preview/image-io/open-folder-missing-path",
                  test_open_containing_folder_rejects_missing_path);
  g_test_add_func("/preview/image-io/open-folder-exact-argv",
                  test_open_containing_folder_uses_exact_argv);
  return g_test_run();
}
