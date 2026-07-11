#include "helper_resolution.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <string.h>

static ShaulaRuntimeHelperSpan span_from_string(const char *value) {
  return (ShaulaRuntimeHelperSpan){value, strlen(value)};
}

static void assert_owned_bytes(const ShaulaRuntimeHelperOwnedPath *actual,
                               const char *expected, size_t expected_length) {
  g_assert_nonnull(actual->data);
  g_assert_cmpuint(actual->length, ==, expected_length);
  g_assert_cmpmem(actual->data, actual->length, expected, expected_length);
  g_assert_cmpint(actual->data[actual->length], ==, '\0');
}

static void assert_owned_string(const ShaulaRuntimeHelperOwnedPath *actual,
                                const char *expected) {
  assert_owned_bytes(actual, expected, strlen(expected));
}

static void write_non_executable_file(const char *path) {
  g_autoptr(GError) error = NULL;

  g_assert_true(g_file_set_contents(path, "helper", 6, &error));
  g_assert_no_error(error);
  g_assert_cmpint(g_chmod(path, 0644), ==, 0);
}

static void test_override_table(void) {
  static const struct {
    const char *name;
    const char *override_value;
    const char *expected;
  } cases[] = {
      {"absolute", " \t/opt/shaula helper\r\n", "/opt/shaula helper"},
      {"relative", " ../helpers/shaula-preview ", "../helpers/shaula-preview"},
      {"shell-bytes", " helper;$()'\" ", "helper;$()'\""},
      {"non-ascii", " \xE6\x96\x87/helper ", "\xE6\x96\x87/helper"},
  };
  size_t index;

  for (index = 0; index < G_N_ELEMENTS(cases); index += 1) {
    ShaulaRuntimeHelperOwnedPath actual = {0};
    ShaulaRuntimeHelperStatus status;

    g_test_message("case: %s", cases[index].name);
    status = shaula_runtime_helper_resolve(
        cases[index].override_value, span_from_string("/ignored"),
        span_from_string("ignored-helper"), &actual);
    g_assert_cmpint(status, ==, SHAULA_RUNTIME_HELPER_STATUS_OK);
    assert_owned_string(&actual, cases[index].expected);
    shaula_runtime_helper_owned_path_clear(&actual);
    shaula_runtime_helper_owned_path_clear(&actual);
  }
}

static void test_sibling_and_bare_fallback(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-helper-resolution-XXXXXX", &error);
  g_autofree char *helper_path = NULL;
  g_autofree char *directory_helper = NULL;
  g_autofree char *trailing_dir = NULL;
  g_autofree char *expected_trailing = NULL;
  ShaulaRuntimeHelperOwnedPath actual = {0};
  ShaulaRuntimeHelperStatus status;

  g_assert_no_error(error);
  g_assert_nonnull(root);

  helper_path = g_build_filename(root, "shaula-helper", NULL);
  directory_helper = g_build_filename(root, "directory-helper", NULL);
  write_non_executable_file(helper_path);
  g_assert_cmpint(g_mkdir(directory_helper, 0755), ==, 0);

  status = shaula_runtime_helper_resolve(
      NULL, span_from_string(root), span_from_string("shaula-helper"), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_HELPER_STATUS_OK);
  assert_owned_string(&actual, helper_path);
  shaula_runtime_helper_owned_path_clear(&actual);

  status = shaula_runtime_helper_resolve(" \t\r\n", span_from_string(root),
                                         span_from_string("directory-helper"),
                                         &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_HELPER_STATUS_OK);
  assert_owned_string(&actual, directory_helper);
  shaula_runtime_helper_owned_path_clear(&actual);

  status = shaula_runtime_helper_resolve("", span_from_string(root),
                                         span_from_string("missing helper;$()"),
                                         &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_HELPER_STATUS_OK);
  assert_owned_string(&actual, "missing helper;$()");
  shaula_runtime_helper_owned_path_clear(&actual);

  status = shaula_runtime_helper_resolve(
      NULL, (ShaulaRuntimeHelperSpan){NULL, 0},
      span_from_string("shaula-preview"), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_HELPER_STATUS_OK);
  assert_owned_string(&actual, "shaula-preview");
  shaula_runtime_helper_owned_path_clear(&actual);

  trailing_dir = g_strdup_printf("%s/", root);
  expected_trailing = g_strdup_printf("%s//shaula-helper", root);
  status =
      shaula_runtime_helper_resolve(NULL, span_from_string(trailing_dir),
                                    span_from_string("shaula-helper"), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_HELPER_STATUS_OK);
  assert_owned_string(&actual, expected_trailing);
  shaula_runtime_helper_owned_path_clear(&actual);
  g_clear_pointer(&expected_trailing, g_free);

  status = shaula_runtime_helper_resolve(NULL, span_from_string(root),
                                         span_from_string("/shaula-helper"),
                                         &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_HELPER_STATUS_OK);
  expected_trailing = g_strdup_printf("%s//shaula-helper", root);
  assert_owned_string(&actual, expected_trailing);
  shaula_runtime_helper_owned_path_clear(&actual);
  g_clear_pointer(&expected_trailing, g_free);

  status = shaula_runtime_helper_resolve(NULL, span_from_string(root),
                                         span_from_string(""), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_HELPER_STATUS_OK);
  expected_trailing = g_strdup_printf("%s/", root);
  assert_owned_string(&actual, expected_trailing);
  shaula_runtime_helper_owned_path_clear(&actual);

  g_assert_cmpint(g_remove(helper_path), ==, 0);
  g_assert_cmpint(g_rmdir(directory_helper), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_byte_and_error_contract(void) {
  static const char embedded_name[] = {'h', 'e', '\0', 'l', 'p', 'e', 'r'};
  ShaulaRuntimeHelperOwnedPath actual = {(char *)"sentinel", 8};
  ShaulaRuntimeHelperStatus status;

  status = shaula_runtime_helper_resolve(
      NULL, span_from_string("/tmp"),
      (ShaulaRuntimeHelperSpan){embedded_name, sizeof(embedded_name)}, &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_HELPER_STATUS_OK);
  assert_owned_bytes(&actual, embedded_name, sizeof(embedded_name));
  shaula_runtime_helper_owned_path_clear(&actual);

  actual = (ShaulaRuntimeHelperOwnedPath){(char *)"sentinel", 8};
  status = shaula_runtime_helper_resolve(
      NULL, (ShaulaRuntimeHelperSpan){"x", SIZE_MAX},
      span_from_string("helper"), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_HELPER_STATUS_OUT_OF_MEMORY);
  g_assert_null(actual.data);
  g_assert_cmpuint(actual.length, ==, 0);

  actual = (ShaulaRuntimeHelperOwnedPath){(char *)"sentinel", 8};
  status =
      shaula_runtime_helper_resolve(NULL, (ShaulaRuntimeHelperSpan){NULL, 1},
                                    span_from_string("helper"), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_HELPER_STATUS_INVALID_ARGUMENT);
  g_assert_null(actual.data);
  g_assert_cmpuint(actual.length, ==, 0);

  status = shaula_runtime_helper_resolve(
      NULL, (ShaulaRuntimeHelperSpan){NULL, 0},
      (ShaulaRuntimeHelperSpan){NULL, 1}, &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_HELPER_STATUS_INVALID_ARGUMENT);

  g_assert_cmpint(
      shaula_runtime_helper_resolve(NULL, (ShaulaRuntimeHelperSpan){NULL, 0},
                                    span_from_string("helper"), NULL),
      ==, SHAULA_RUNTIME_HELPER_STATUS_INVALID_ARGUMENT);

  shaula_runtime_helper_owned_path_clear(NULL);
  shaula_runtime_helper_owned_path_clear(&actual);
  shaula_runtime_helper_owned_path_clear(&actual);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/runtime/helper-resolution/override", test_override_table);
  g_test_add_func("/runtime/helper-resolution/sibling-fallback",
                  test_sibling_and_bare_fallback);
  g_test_add_func("/runtime/helper-resolution/bytes-errors",
                  test_byte_and_error_contract);
  return g_test_run();
}
