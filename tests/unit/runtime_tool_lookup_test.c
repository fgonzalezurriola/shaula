#include "tool_lookup.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <string.h>

static ShaulaRuntimeToolSpan span_from_string(const char *value) {
  return (ShaulaRuntimeToolSpan){value, strlen(value)};
}

static void write_non_executable_file(const char *path) {
  g_autoptr(GError) error = NULL;

  g_assert_true(g_file_set_contents(path, "tool", 4, &error));
  g_assert_no_error(error);
  g_assert_cmpint(g_chmod(path, 0644), ==, 0);
}

static void assert_owned_path(const ShaulaRuntimeToolOwnedPath *actual,
                              const char *expected) {
  size_t expected_length = strlen(expected);

  g_assert_nonnull(actual->data);
  g_assert_cmpuint(actual->length, ==, expected_length);
  g_assert_cmpmem(actual->data, actual->length, expected, expected_length);
  g_assert_cmpint(actual->data[actual->length], ==, '\0');
}

static void test_path_exists_contract(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-tool-path-exists-XXXXXX", &error);
  g_autofree char *file_path = NULL;
  g_autofree char *dir_path = NULL;
  g_autofree char *missing_path = NULL;
  g_autofree char *old_cwd = NULL;
  static const char embedded_nul[] = {'a', '\0', 'b'};

  g_assert_no_error(error);
  g_assert_nonnull(root);

  file_path = g_build_filename(root, "plain-file", NULL);
  dir_path = g_build_filename(root, "plain-dir", NULL);
  missing_path = g_build_filename(root, "missing", NULL);
  write_non_executable_file(file_path);
  g_assert_cmpint(g_mkdir(dir_path, 0755), ==, 0);

  g_assert_cmpint(shaula_runtime_tool_path_exists(span_from_string(file_path)),
                  ==, 1);
  g_assert_cmpint(shaula_runtime_tool_path_exists(span_from_string(dir_path)),
                  ==, 1);
  g_assert_cmpint(
      shaula_runtime_tool_path_exists(span_from_string(missing_path)), ==, 0);
  g_assert_cmpint(shaula_runtime_tool_path_exists(span_from_string("")), ==, 0);
  g_assert_cmpint(shaula_runtime_tool_path_exists((ShaulaRuntimeToolSpan){
                      embedded_nul, sizeof(embedded_nul)}),
                  ==, 0);
  g_assert_cmpint(
      shaula_runtime_tool_path_exists((ShaulaRuntimeToolSpan){NULL, 1}), ==, 0);

  old_cwd = g_get_current_dir();
  g_assert_cmpint(g_chdir(root), ==, 0);
  g_assert_cmpint(
      shaula_runtime_tool_path_exists(span_from_string("plain-file")), ==, 1);
  g_assert_cmpint(g_chdir(old_cwd), ==, 0);

  g_assert_cmpint(g_remove(file_path), ==, 0);
  g_assert_cmpint(g_rmdir(dir_path), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_find_absolute_contract(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root = g_dir_make_tmp("shaula-tool-absolute-XXXXXX", &error);
  g_autofree char *first = NULL;
  g_autofree char *second = NULL;
  g_autofree char *missing = NULL;
  ShaulaRuntimeToolSpan candidates[5];
  ShaulaRuntimeToolSpan actual = {0};
  ShaulaRuntimeToolLookupStatus status;

  g_assert_no_error(error);
  g_assert_nonnull(root);

  first = g_build_filename(root, "first", NULL);
  second = g_build_filename(root, "second", NULL);
  missing = g_build_filename(root, "missing", NULL);
  write_non_executable_file(first);
  write_non_executable_file(second);

  candidates[0] = span_from_string("relative-existing-is-not-absolute");
  candidates[1] = span_from_string("");
  candidates[2] = span_from_string(missing);
  candidates[3] = span_from_string(first);
  candidates[4] = span_from_string(second);

  status = shaula_runtime_tool_find_absolute(candidates,
                                             G_N_ELEMENTS(candidates), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK);
  g_assert_true(actual.data == first);
  g_assert_cmpuint(actual.length, ==, strlen(first));

  actual = (ShaulaRuntimeToolSpan){"sentinel", 8};
  status = shaula_runtime_tool_find_absolute(candidates, 3, &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_NOT_FOUND);
  g_assert_null(actual.data);
  g_assert_cmpuint(actual.length, ==, 0);

  g_assert_cmpint(shaula_runtime_tool_find_absolute(NULL, 0, &actual), ==,
                  SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_NOT_FOUND);
  g_assert_cmpint(shaula_runtime_tool_find_absolute(NULL, 1, &actual), ==,
                  SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(shaula_runtime_tool_find_absolute(candidates, 1, NULL), ==,
                  SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_INVALID_ARGUMENT);

  candidates[0] = (ShaulaRuntimeToolSpan){NULL, 1};
  g_assert_cmpint(shaula_runtime_tool_find_absolute(candidates, 1, &actual), ==,
                  SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_INVALID_ARGUMENT);

  g_assert_cmpint(g_remove(first), ==, 0);
  g_assert_cmpint(g_remove(second), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_grim_candidate_order(void) {
  static const char *const expected_candidates[] = {
      "/usr/bin/grim",
      "/bin/grim",
      "/usr/local/bin/grim",
  };
  const char *expected = NULL;
  ShaulaRuntimeToolSpan actual = {0};
  ShaulaRuntimeToolLookupStatus status;
  size_t index;

  for (index = 0; index < G_N_ELEMENTS(expected_candidates); index += 1) {
    if (shaula_runtime_tool_path_exists(
            span_from_string(expected_candidates[index]))) {
      expected = expected_candidates[index];
      break;
    }
  }

  status = shaula_runtime_tool_grim_path(&actual);
  if (expected == NULL) {
    g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_NOT_FOUND);
    g_assert_null(actual.data);
    g_assert_cmpuint(actual.length, ==, 0);
  } else {
    g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK);
    g_assert_cmpmem(actual.data, actual.length, expected, strlen(expected));
  }

  g_assert_cmpint(shaula_runtime_tool_grim_path(NULL), ==,
                  SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_INVALID_ARGUMENT);
}

static void test_find_in_path_table(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-tool-path-table-XXXXXX", &error);
  g_autofree char *first_dir = NULL;
  g_autofree char *second_dir = NULL;
  g_autofree char *relative_dir = NULL;
  g_autofree char *space_dir = NULL;
  g_autofree char *whitespace_dir = NULL;
  g_autofree char *second_tool = NULL;
  g_autofree char *relative_tool = NULL;
  g_autofree char *special_tool = NULL;
  g_autofree char *whitespace_tool = NULL;
  g_autofree char *missing_path = NULL;
  g_autofree char *ordered_path = NULL;
  g_autofree char *trailing_separator_path = NULL;
  g_autofree char *special_path = NULL;
  g_autofree char *old_cwd = NULL;
  g_autofree char *expected = NULL;
  ShaulaRuntimeToolOwnedPath actual = {0};
  ShaulaRuntimeToolLookupStatus status;
  static const char special_name[] = "tool name;$()\xC3\xA9";

  g_assert_no_error(error);
  g_assert_nonnull(root);

  first_dir = g_build_filename(root, "first", NULL);
  second_dir = g_build_filename(root, "second", NULL);
  relative_dir = g_build_filename(root, "relative", NULL);
  space_dir = g_build_filename(root, "space dir", NULL);
  whitespace_dir = g_build_filename(root, " ", NULL);
  g_assert_cmpint(g_mkdir(first_dir, 0755), ==, 0);
  g_assert_cmpint(g_mkdir(second_dir, 0755), ==, 0);
  g_assert_cmpint(g_mkdir(relative_dir, 0755), ==, 0);
  g_assert_cmpint(g_mkdir(space_dir, 0755), ==, 0);
  g_assert_cmpint(g_mkdir(whitespace_dir, 0755), ==, 0);

  second_tool = g_build_filename(second_dir, "tool", NULL);
  relative_tool = g_build_filename(relative_dir, "tool", NULL);
  special_tool = g_build_filename(space_dir, special_name, NULL);
  whitespace_tool = g_build_filename(whitespace_dir, "tool", NULL);
  write_non_executable_file(second_tool);
  write_non_executable_file(relative_tool);
  write_non_executable_file(special_tool);
  write_non_executable_file(whitespace_tool);

  missing_path = g_build_filename(root, "missing", NULL);
  ordered_path =
      g_strdup_printf("%s:%s:%s", first_dir, missing_path, second_dir);
  status = shaula_runtime_tool_find_in_path(ordered_path,
                                            span_from_string("tool"), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK);
  assert_owned_path(&actual, second_tool);
  shaula_runtime_tool_owned_path_clear(&actual);
  shaula_runtime_tool_owned_path_clear(&actual);

  trailing_separator_path = g_strdup_printf("%s/", second_dir);
  expected = g_strdup_printf("%s//tool", second_dir);
  status = shaula_runtime_tool_find_in_path(trailing_separator_path,
                                            span_from_string("tool"), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK);
  assert_owned_path(&actual, expected);
  shaula_runtime_tool_owned_path_clear(&actual);
  g_clear_pointer(&expected, g_free);

  status = shaula_runtime_tool_find_in_path(second_dir,
                                            span_from_string("/tool"), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK);
  expected = g_strdup_printf("%s//tool", second_dir);
  assert_owned_path(&actual, expected);
  shaula_runtime_tool_owned_path_clear(&actual);
  g_clear_pointer(&expected, g_free);

  status = shaula_runtime_tool_find_in_path(second_dir, span_from_string(""),
                                            &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK);
  expected = g_strdup_printf("%s/", second_dir);
  assert_owned_path(&actual, expected);
  shaula_runtime_tool_owned_path_clear(&actual);
  g_clear_pointer(&expected, g_free);

  special_path = g_strdup_printf(":%s::", space_dir);
  status = shaula_runtime_tool_find_in_path(
      special_path, span_from_string(special_name), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK);
  assert_owned_path(&actual, special_tool);
  shaula_runtime_tool_owned_path_clear(&actual);

  old_cwd = g_get_current_dir();
  g_assert_cmpint(g_chdir(root), ==, 0);

  status = shaula_runtime_tool_find_in_path(
      ":first::", span_from_string("tool"), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_NOT_FOUND);
  g_assert_null(actual.data);

  status = shaula_runtime_tool_find_in_path("relative",
                                            span_from_string("tool"), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK);
  assert_owned_path(&actual, "relative/tool");
  shaula_runtime_tool_owned_path_clear(&actual);

  status =
      shaula_runtime_tool_find_in_path(" ", span_from_string("tool"), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK);
  assert_owned_path(&actual, " /tool");
  shaula_runtime_tool_owned_path_clear(&actual);

  status = shaula_runtime_tool_find_in_path("relative/../second",
                                            span_from_string("tool"), &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK);
  assert_owned_path(&actual, "relative/../second/tool");
  shaula_runtime_tool_owned_path_clear(&actual);

  g_assert_cmpint(g_chdir(old_cwd), ==, 0);

  g_assert_cmpint(
      shaula_runtime_tool_find_in_path(NULL, span_from_string("tool"), &actual),
      ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_NOT_FOUND);
  g_assert_cmpint(
      shaula_runtime_tool_find_in_path("", span_from_string("tool"), &actual),
      ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_NOT_FOUND);

  g_assert_cmpint(g_remove(second_tool), ==, 0);
  g_assert_cmpint(g_remove(relative_tool), ==, 0);
  g_assert_cmpint(g_remove(special_tool), ==, 0);
  g_assert_cmpint(g_remove(whitespace_tool), ==, 0);
  g_assert_cmpint(g_rmdir(first_dir), ==, 0);
  g_assert_cmpint(g_rmdir(second_dir), ==, 0);
  g_assert_cmpint(g_rmdir(relative_dir), ==, 0);
  g_assert_cmpint(g_rmdir(space_dir), ==, 0);
  g_assert_cmpint(g_rmdir(whitespace_dir), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_find_in_path_errors_and_bytes(void) {
  ShaulaRuntimeToolOwnedPath actual = {(char *)"sentinel", 8};
  ShaulaRuntimeToolLookupStatus status;
  static const char embedded_nul[] = {'t', '\0', 'o', 'o', 'l'};

  status = shaula_runtime_tool_find_in_path(
      "/tmp", (ShaulaRuntimeToolSpan){NULL, 1}, &actual);
  g_assert_cmpint(status, ==,
                  SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_INVALID_ARGUMENT);
  g_assert_null(actual.data);
  g_assert_cmpuint(actual.length, ==, 0);

  actual = (ShaulaRuntimeToolOwnedPath){(char *)"sentinel", 8};
  status = shaula_runtime_tool_find_in_path(
      "/tmp", (ShaulaRuntimeToolSpan){"x", SIZE_MAX}, &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OUT_OF_MEMORY);
  g_assert_null(actual.data);
  g_assert_cmpuint(actual.length, ==, 0);

  status = shaula_runtime_tool_find_in_path(
      "/tmp", (ShaulaRuntimeToolSpan){embedded_nul, sizeof(embedded_nul)},
      &actual);
  g_assert_cmpint(status, ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_NOT_FOUND);
  g_assert_null(actual.data);

  g_assert_cmpint(
      shaula_runtime_tool_find_in_path("/tmp", span_from_string("tool"), NULL),
      ==, SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_INVALID_ARGUMENT);

  shaula_runtime_tool_owned_path_clear(NULL);
  shaula_runtime_tool_owned_path_clear(&actual);
  shaula_runtime_tool_owned_path_clear(&actual);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/runtime/tool-lookup/path-exists",
                  test_path_exists_contract);
  g_test_add_func("/runtime/tool-lookup/find-absolute",
                  test_find_absolute_contract);
  g_test_add_func("/runtime/tool-lookup/grim-order", test_grim_candidate_order);
  g_test_add_func("/runtime/tool-lookup/find-in-path-table",
                  test_find_in_path_table);
  g_test_add_func("/runtime/tool-lookup/find-in-path-errors",
                  test_find_in_path_errors_and_bytes);
  return g_test_run();
}
