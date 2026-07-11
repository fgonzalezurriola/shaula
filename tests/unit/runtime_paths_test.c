#include "paths.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <string.h>

static ShaulaRuntimePathSpan span_from_string(const char *value) {
  return (ShaulaRuntimePathSpan){value, strlen(value)};
}

static void assert_owned_path(const ShaulaRuntimeOwnedPath *actual,
                              const char *expected, size_t expected_length) {
  g_assert_nonnull(actual->data);
  g_assert_cmpuint(actual->length, ==, expected_length);
  g_assert_cmpmem(actual->data, actual->length, expected, expected_length);
  g_assert_cmpint(actual->data[actual->length], ==, '\0');
}

static void test_resolve_table(void) {
  static const struct {
    const char *name;
    const char *override_value;
    const char *runtime_dir;
    const char *relative_path;
    const char *expected;
  } cases[] = {
      {"override-wins", " /custom/state.v1\t", "/ignored", "overlay/state.v1",
       "/custom/state.v1"},
      {"relative-override-preserved", " ../state//. ", "/ignored",
       "overlay/state.v1", "../state//."},
      {"empty-override", "", "/run/user/1000", "overlay/state.v1",
       "/run/user/1000/shaula/overlay/state.v1"},
      {"whitespace-override", " \t\r\n", "/run/user/1000", "overlay/state.v1",
       "/run/user/1000/shaula/overlay/state.v1"},
      {"runtime-trimmed", NULL, " \t/run/user/1000\r\n", "captures",
       "/run/user/1000/shaula/captures"},
      {"runtime-trailing-slash", NULL, "/run/user/1000/", "captures",
       "/run/user/1000//shaula/captures"},
      {"runtime-relative", NULL, "runtime", "./state/../file",
       "runtime/shaula/./state/../file"},
      {"missing-runtime", NULL, NULL, "overlay/state.v1",
       "/tmp/shaula/overlay/state.v1"},
      {"empty-runtime", NULL, "", "captures", "/tmp/shaula/captures"},
      {"whitespace-runtime", NULL, " \t\r\n", "captures",
       "/tmp/shaula/captures"},
      {"empty-relative", NULL, NULL, "", "/tmp/shaula/"},
      {"absolute-looking-relative", NULL, NULL, "/captures",
       "/tmp/shaula//captures"},
      {"repeated-separators", NULL, NULL, "overlay//state.v1",
       "/tmp/shaula/overlay//state.v1"},
      {"non-ascii", NULL, "/tmp/\xC3\xA9", "\xE6\x96\x87/state",
       "/tmp/\xC3\xA9/shaula/\xE6\x96\x87/state"},
  };
  size_t index;

  for (index = 0; index < G_N_ELEMENTS(cases); index += 1) {
    ShaulaRuntimeOwnedPath actual = {0};
    ShaulaRuntimePathStatus status;

    g_test_message("case: %s", cases[index].name);
    status = shaula_runtime_path_resolve(
        cases[index].override_value, cases[index].runtime_dir,
        span_from_string(cases[index].relative_path), &actual);
    g_assert_cmpint(status, ==, SHAULA_RUNTIME_PATH_STATUS_OK);
    assert_owned_path(&actual, cases[index].expected,
                      strlen(cases[index].expected));
    shaula_runtime_owned_path_clear(&actual);
    g_assert_null(actual.data);
    g_assert_cmpuint(actual.length, ==, 0);
    shaula_runtime_owned_path_clear(&actual);
  }
}

static void test_resolve_embedded_nul_and_overflow(void) {
  static const char relative[] = {'a', '\0', 'b'};
  static const char expected[] = "/tmp/shaula/a\0b";
  ShaulaRuntimeOwnedPath actual = {0};
  ShaulaRuntimePathStatus status;

  status = shaula_runtime_path_resolve(
      NULL, NULL, (ShaulaRuntimePathSpan){relative, sizeof(relative)}, &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_PATH_STATUS_OK);
  assert_owned_path(&actual, expected, sizeof(expected) - 1);
  shaula_runtime_owned_path_clear(&actual);

  actual = (ShaulaRuntimeOwnedPath){(char *)"sentinel", 8};
  status = shaula_runtime_path_resolve(
      NULL, NULL, (ShaulaRuntimePathSpan){"x", SIZE_MAX}, &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_PATH_STATUS_OUT_OF_MEMORY);
  g_assert_null(actual.data);
  g_assert_cmpuint(actual.length, ==, 0);

  actual = (ShaulaRuntimeOwnedPath){(char *)"sentinel", 8};
  status = shaula_runtime_path_resolve(
      NULL, NULL, (ShaulaRuntimePathSpan){NULL, 1}, &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_PATH_STATUS_INVALID_ARGUMENT);
  g_assert_null(actual.data);
  g_assert_cmpuint(actual.length, ==, 0);

  g_assert_cmpint(shaula_runtime_path_resolve(
                      NULL, NULL, span_from_string("captures"), NULL),
                  ==, SHAULA_RUNTIME_PATH_STATUS_INVALID_ARGUMENT);
}

static void test_capture_artifact_table(void) {
  static const struct {
    const char *name;
    const char *path;
    int32_t expected;
  } cases[] = {
      {"tmp-capture", "/tmp/shaula/captures/image.png", 1},
      {"runtime-capture", "/run/user/1000/shaula/captures/image.png", 1},
      {"relative-containing-marker", "prefix/shaula/captures/image.png", 1},
      {"durable", "/home/me/Pictures/shaula/image.png", 0},
      {"directory-without-trailing-separator", "/tmp/shaula/captures", 0},
      {"similar-name", "/tmp/shaula/captures-other/image.png", 0},
      {"empty", "", 0},
  };
  size_t index;

  for (index = 0; index < G_N_ELEMENTS(cases); index += 1) {
    g_test_message("case: %s", cases[index].name);
    g_assert_cmpint(shaula_runtime_path_is_capture_artifact(
                        span_from_string(cases[index].path)),
                    ==, cases[index].expected);
  }

  {
    static const char embedded[] = {'x', '\0', '/', 's', 'h', 'a', 'u',
                                    'l', 'a',  '/', 'c', 'a', 'p', 't',
                                    'u', 'r',  'e', 's', '/', 'y'};
    g_assert_cmpint(shaula_runtime_path_is_capture_artifact(
                        (ShaulaRuntimePathSpan){embedded, sizeof(embedded)}),
                    ==, 1);
  }

  g_assert_cmpint(
      shaula_runtime_path_is_capture_artifact((ShaulaRuntimePathSpan){NULL, 1}),
      ==, 0);
}

static void test_ensure_parent(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root = g_dir_make_tmp("shaula-runtime-paths-XXXXXX", &error);
  g_autofree char *nested_path = NULL;
  g_autofree char *nested_dir = NULL;
  g_autofree char *nested_parent = NULL;
  g_autofree char *repeated_path = NULL;
  g_autofree char *repeated_dir = NULL;
  g_autofree char *repeated_parent = NULL;
  g_autofree char *dotdot_path = NULL;
  g_autofree char *trailing_path = NULL;
  g_autofree char *trailing_parent = NULL;
  g_autofree char *trailing_child = NULL;
  g_autofree char *first_dir = NULL;
  g_autofree char *second_dir = NULL;
  g_autofree char *blocker = NULL;
  g_autofree char *blocked_path = NULL;
  static const char embedded_nul[] = {'a', '/', 'b', '\0', '/', 'c'};

  g_assert_no_error(error);
  g_assert_nonnull(root);

  nested_path = g_build_filename(root, "nested", "child", "file", NULL);
  nested_dir = g_build_filename(root, "nested", "child", NULL);
  nested_parent = g_build_filename(root, "nested", NULL);
  g_assert_cmpint(
      shaula_runtime_path_ensure_parent(span_from_string(nested_path)), ==,
      SHAULA_RUNTIME_PATH_STATUS_OK);
  g_assert_true(g_file_test(nested_dir, G_FILE_TEST_IS_DIR));

  repeated_path = g_strdup_printf("%s/repeated//inner/file", root);
  repeated_dir = g_build_filename(root, "repeated", "inner", NULL);
  repeated_parent = g_build_filename(root, "repeated", NULL);
  g_assert_cmpint(
      shaula_runtime_path_ensure_parent(span_from_string(repeated_path)), ==,
      SHAULA_RUNTIME_PATH_STATUS_OK);
  g_assert_true(g_file_test(repeated_dir, G_FILE_TEST_IS_DIR));

  dotdot_path = g_strdup_printf("%s/first/../second/file", root);
  first_dir = g_build_filename(root, "first", NULL);
  second_dir = g_build_filename(root, "second", NULL);
  g_assert_cmpint(
      shaula_runtime_path_ensure_parent(span_from_string(dotdot_path)), ==,
      SHAULA_RUNTIME_PATH_STATUS_OK);
  g_assert_true(g_file_test(first_dir, G_FILE_TEST_IS_DIR));
  g_assert_true(g_file_test(second_dir, G_FILE_TEST_IS_DIR));

  trailing_path = g_strdup_printf("%s/trailing/child/", root);
  trailing_parent = g_build_filename(root, "trailing", NULL);
  trailing_child = g_build_filename(root, "trailing", "child", NULL);
  g_assert_cmpint(
      shaula_runtime_path_ensure_parent(span_from_string(trailing_path)), ==,
      SHAULA_RUNTIME_PATH_STATUS_OK);
  g_assert_true(g_file_test(trailing_parent, G_FILE_TEST_IS_DIR));
  g_assert_false(g_file_test(trailing_child, G_FILE_TEST_EXISTS));

  g_assert_cmpint(
      shaula_runtime_path_ensure_parent(span_from_string("single-component")),
      ==, SHAULA_RUNTIME_PATH_STATUS_OK);
  g_assert_cmpint(shaula_runtime_path_ensure_parent(span_from_string("/")), ==,
                  SHAULA_RUNTIME_PATH_STATUS_OK);
  g_assert_cmpint(shaula_runtime_path_ensure_parent((ShaulaRuntimePathSpan){
                      embedded_nul, sizeof(embedded_nul)}),
                  ==, SHAULA_RUNTIME_PATH_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(
      shaula_runtime_path_ensure_parent((ShaulaRuntimePathSpan){NULL, 1}), ==,
      SHAULA_RUNTIME_PATH_STATUS_INVALID_ARGUMENT);

  blocker = g_build_filename(root, "blocker", NULL);
  g_assert_true(g_file_set_contents(blocker, "x", 1, &error));
  g_assert_no_error(error);
  blocked_path = g_build_filename(blocker, "child", "file", NULL);
  g_assert_cmpint(
      shaula_runtime_path_ensure_parent(span_from_string(blocked_path)), ==,
      SHAULA_RUNTIME_PATH_STATUS_FILESYSTEM_ERROR);

  g_assert_cmpint(g_remove(blocker), ==, 0);
  g_assert_cmpint(g_rmdir(nested_dir), ==, 0);
  g_assert_cmpint(g_rmdir(nested_parent), ==, 0);
  g_assert_cmpint(g_rmdir(repeated_dir), ==, 0);
  g_assert_cmpint(g_rmdir(repeated_parent), ==, 0);
  g_assert_cmpint(g_rmdir(second_dir), ==, 0);
  g_assert_cmpint(g_rmdir(first_dir), ==, 0);
  g_assert_cmpint(g_rmdir(trailing_parent), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/runtime/paths/resolve-table", test_resolve_table);
  g_test_add_func("/runtime/paths/resolve-nul-overflow",
                  test_resolve_embedded_nul_and_overflow);
  g_test_add_func("/runtime/paths/capture-artifact",
                  test_capture_artifact_table);
  g_test_add_func("/runtime/paths/ensure-parent", test_ensure_parent);
  return g_test_run();
}
