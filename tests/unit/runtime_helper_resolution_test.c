#include "helper_resolution.h"

#include <glib.h>
#include <glib/gstdio.h>

static void test_current_path_and_helper_precedence(void) {
  g_autofree char *current = shaula_executable_current_path();
  g_autofree char *old_override = g_strdup(g_getenv("SHAULA_TEST_HELPER"));
  g_autofree char *resolved = NULL;

  g_assert_nonnull(current);
  g_assert_true(g_path_is_absolute(current));

  g_assert_true(g_setenv("SHAULA_TEST_HELPER", "  /tmp/custom-helper \n", TRUE));
  resolved =
      shaula_executable_resolve_helper("SHAULA_TEST_HELPER", "missing-helper");
  g_assert_cmpstr(resolved, ==, "/tmp/custom-helper");
  g_clear_pointer(&resolved, g_free);

  g_assert_true(g_setenv("SHAULA_TEST_HELPER", " \t\r\n", TRUE));
  resolved = shaula_executable_resolve_helper(
      "SHAULA_TEST_HELPER", "shaula-helper-that-does-not-exist");
  g_assert_cmpstr(resolved, ==, "shaula-helper-that-does-not-exist");
  g_clear_pointer(&resolved, g_free);

  g_assert_null(shaula_executable_resolve_helper(NULL, "helper"));
  g_assert_null(shaula_executable_resolve_helper("ENV", ""));

  if (old_override != NULL) {
    g_assert_true(g_setenv("SHAULA_TEST_HELPER", old_override, TRUE));
  } else {
    g_unsetenv("SHAULA_TEST_HELPER");
  }
}

static void test_tool_candidates_and_path(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-executable-discovery-XXXXXX", &error);
  g_autofree char *first = NULL;
  g_autofree char *second = NULL;
  g_autofree char *path_tool = NULL;
  g_autofree char *old_path = g_strdup(g_getenv("PATH"));
  g_autofree char *resolved = NULL;

  g_assert_no_error(error);
  g_assert_nonnull(root);
  first = g_build_filename(root, "first", NULL);
  second = g_build_filename(root, "second", NULL);
  path_tool = g_build_filename(root, "path-tool", NULL);
  g_assert_true(g_file_set_contents(second, "", 0, &error));
  g_assert_no_error(error);
  g_assert_true(g_file_set_contents(path_tool, "", 0, &error));
  g_assert_no_error(error);

  const char *candidates[] = {first, second};
  resolved = shaula_executable_find_tool("unused", candidates,
                                         G_N_ELEMENTS(candidates));
  g_assert_cmpstr(resolved, ==, second);
  g_clear_pointer(&resolved, g_free);

  g_assert_true(g_setenv("PATH", root, TRUE));
  resolved = shaula_executable_find_program("path-tool");
  g_assert_cmpstr(resolved, ==, path_tool);
  g_clear_pointer(&resolved, g_free);
  g_assert_null(shaula_executable_find_program("missing"));
  g_assert_null(shaula_executable_find_tool(NULL, NULL, 0));

  if (old_path != NULL) {
    g_assert_true(g_setenv("PATH", old_path, TRUE));
  } else {
    g_unsetenv("PATH");
  }
  g_assert_cmpint(g_remove(second), ==, 0);
  g_assert_cmpint(g_remove(path_tool), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/runtime/executable/helper-precedence",
                  test_current_path_and_helper_precedence);
  g_test_add_func("/runtime/executable/tool-discovery",
                  test_tool_candidates_and_path);
  return g_test_run();
}
