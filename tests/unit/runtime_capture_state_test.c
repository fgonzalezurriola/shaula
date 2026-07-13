#include "capture_state.h"

#include <glib.h>
#include <glib/gstdio.h>

static void test_capture_state_surface(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root = g_dir_make_tmp("shaula-capture-state-XXXXXX", &error);
  g_autofree char *old_runtime = g_strdup(g_getenv("XDG_RUNTIME_DIR"));
  g_autofree char *capture_directory = NULL;
  g_autofree char *overlay_path = NULL;
  g_autofree char *state_directory = NULL;
  g_autofree char *previous_path = NULL;
  g_autofree char *overlay_directory = NULL;
  ShaulaCaptureStateSession *first = NULL;
  ShaulaCaptureStateSession *second = NULL;
  ShaulaCaptureStateGeometry geometry = {0};
  int32_t present = 0;

  g_assert_no_error(error);
  g_assert_nonnull(root);
  g_assert_true(g_setenv("XDG_RUNTIME_DIR", root, TRUE));

  capture_directory = shaula_capture_state_capture_directory();
  g_assert_nonnull(capture_directory);
  g_assert_true(g_file_test(capture_directory, G_FILE_TEST_IS_DIR));

  overlay_path = shaula_capture_state_overlay_background_path(42);
  g_assert_nonnull(overlay_path);
  g_assert_true(g_str_has_suffix(overlay_path, "/overlay/background-42.png"));
  overlay_directory = g_path_get_dirname(overlay_path);
  g_assert_true(g_file_test(overlay_directory, G_FILE_TEST_IS_DIR));

  g_assert_cmpint(shaula_capture_state_session_acquire(&first), ==,
                  SHAULA_CAPTURE_STATE_STATUS_OK);
  g_assert_nonnull(first);
  g_assert_cmpint(shaula_capture_state_session_acquire(&second), ==,
                  SHAULA_CAPTURE_STATE_STATUS_BUSY);
  g_assert_null(second);
  shaula_capture_state_session_release(first);
  first = NULL;
  g_assert_cmpint(shaula_capture_state_session_acquire(&second), ==,
                  SHAULA_CAPTURE_STATE_STATUS_OK);
  shaula_capture_state_session_release(second);
  second = NULL;

  g_assert_cmpint(shaula_capture_state_previous_load(&present, &geometry), ==,
                  SHAULA_CAPTURE_STATE_STATUS_OK);
  g_assert_cmpint(present, ==, 0);

  geometry = (ShaulaCaptureStateGeometry){
      .x = -10,
      .y = 20,
      .width = 640,
      .height = 480,
  };
  g_assert_cmpint(shaula_capture_state_previous_store(geometry), ==,
                  SHAULA_CAPTURE_STATE_STATUS_OK);
  geometry = (ShaulaCaptureStateGeometry){0};
  g_assert_cmpint(shaula_capture_state_previous_load(&present, &geometry), ==,
                  SHAULA_CAPTURE_STATE_STATUS_OK);
  g_assert_cmpint(present, ==, 1);
  g_assert_cmpint(geometry.x, ==, -10);
  g_assert_cmpint(geometry.y, ==, 20);
  g_assert_cmpuint(geometry.width, ==, 640);
  g_assert_cmpuint(geometry.height, ==, 480);

  state_directory = g_build_filename(root, "shaula", NULL);
  previous_path = g_build_filename(state_directory, "previous-area.v1", NULL);
  g_assert_cmpint(g_remove(previous_path), ==, 0);
  g_assert_cmpint(g_rmdir(capture_directory), ==, 0);
  g_assert_cmpint(g_rmdir(overlay_directory), ==, 0);
  g_assert_cmpint(g_rmdir(state_directory), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);

  if (old_runtime != NULL) {
    g_assert_true(g_setenv("XDG_RUNTIME_DIR", old_runtime, TRUE));
  } else {
    g_unsetenv("XDG_RUNTIME_DIR");
  }
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/runtime/capture-state/surface", test_capture_state_surface);
  return g_test_run();
}
