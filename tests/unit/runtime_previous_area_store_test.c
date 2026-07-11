#include "previous_area_store.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <string.h>

static ShaulaPreviousAreaSpan span_from_string(const char *value) {
  return (ShaulaPreviousAreaSpan){value, strlen(value)};
}

static void assert_geometry(ShaulaPreviousAreaGeometry actual,
                            ShaulaPreviousAreaGeometry expected) {
  g_assert_cmpint(actual.x, ==, expected.x);
  g_assert_cmpint(actual.y, ==, expected.y);
  g_assert_cmpuint(actual.width, ==, expected.width);
  g_assert_cmpuint(actual.height, ==, expected.height);
}

static void test_store_exact_format_and_round_trip(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-previous-area-store-XXXXXX", &error);
  g_autofree char *first_dir = NULL;
  g_autofree char *second_dir = NULL;
  g_autofree char *path = NULL;
  g_autofree char *relative_dir = NULL;
  g_autofree char *relative_path = NULL;
  g_autofree char *raw = NULL;
  g_autofree char *old_cwd = NULL;
  gsize raw_length = 0;
  int32_t present = 0;
  ShaulaPreviousAreaGeometry loaded = {0};
  ShaulaPreviousAreaGeometry extreme = {
      INT32_MIN,
      INT32_MAX,
      UINT32_MAX,
      1,
  };
  ShaulaPreviousAreaGeometry simple = {1, 2, 3, 4};
  ShaulaPreviousAreaGeometry zero_width = {1, 2, 0, 4};
  ShaulaPreviousAreaStatus status;

  g_assert_no_error(error);
  g_assert_nonnull(root);

  first_dir = g_build_filename(root, "selection", NULL);
  second_dir = g_build_filename(first_dir, "nested", NULL);
  path = g_build_filename(second_dir, "previous-area.v1", NULL);

  status = shaula_previous_area_store(span_from_string(path), extreme);
  g_assert_cmpint(status, ==, SHAULA_PREVIOUS_AREA_STATUS_OK);
  g_assert_true(g_file_get_contents(path, &raw, &raw_length, &error));
  g_assert_no_error(error);
  g_assert_cmpuint(raw_length, ==,
                   strlen("-2147483648|2147483647|4294967295|1\n"));
  g_assert_cmpmem(raw, raw_length, "-2147483648|2147483647|4294967295|1\n",
                  strlen("-2147483648|2147483647|4294967295|1\n"));

  status = shaula_previous_area_load(span_from_string(path), &present, &loaded);
  g_assert_cmpint(status, ==, SHAULA_PREVIOUS_AREA_STATUS_OK);
  g_assert_cmpint(present, ==, 1);
  assert_geometry(loaded, extreme);

  g_clear_pointer(&raw, g_free);
  g_assert_true(g_file_set_contents(path, "long stale contents", -1, &error));
  g_assert_no_error(error);
  status = shaula_previous_area_store(span_from_string(path), simple);
  g_assert_cmpint(status, ==, SHAULA_PREVIOUS_AREA_STATUS_OK);
  g_assert_true(g_file_get_contents(path, &raw, &raw_length, &error));
  g_assert_no_error(error);
  g_assert_cmpuint(raw_length, ==, strlen("1|2|3|4\n"));
  g_assert_cmpmem(raw, raw_length, "1|2|3|4\n", strlen("1|2|3|4\n"));

  g_clear_pointer(&raw, g_free);
  status = shaula_previous_area_store(span_from_string(path), zero_width);
  g_assert_cmpint(status, ==, SHAULA_PREVIOUS_AREA_STATUS_OK);
  g_assert_true(g_file_get_contents(path, &raw, &raw_length, &error));
  g_assert_no_error(error);
  g_assert_cmpuint(raw_length, ==, strlen("1|2|0|4\n"));
  g_assert_cmpmem(raw, raw_length, "1|2|0|4\n", strlen("1|2|0|4\n"));
  present = 7;
  loaded = (ShaulaPreviousAreaGeometry){9, 9, 9, 9};
  status = shaula_previous_area_load(span_from_string(path), &present, &loaded);
  g_assert_cmpint(status, ==, SHAULA_PREVIOUS_AREA_STATUS_OK);
  g_assert_cmpint(present, ==, 0);
  assert_geometry(loaded, (ShaulaPreviousAreaGeometry){0});

  relative_dir = g_build_filename(root, "relative", NULL);
  relative_path = g_build_filename(relative_dir, "state", NULL);
  old_cwd = g_get_current_dir();
  g_assert_cmpint(g_chdir(root), ==, 0);
  status =
      shaula_previous_area_store(span_from_string("relative/state"), simple);
  g_assert_cmpint(status, ==, SHAULA_PREVIOUS_AREA_STATUS_OK);
  present = 0;
  loaded = (ShaulaPreviousAreaGeometry){0};
  status = shaula_previous_area_load(span_from_string("relative/state"),
                                     &present, &loaded);
  g_assert_cmpint(status, ==, SHAULA_PREVIOUS_AREA_STATUS_OK);
  g_assert_cmpint(present, ==, 1);
  assert_geometry(loaded, simple);
  g_assert_cmpint(g_chdir(old_cwd), ==, 0);

  g_assert_cmpint(g_remove(path), ==, 0);
  g_assert_cmpint(g_remove(relative_path), ==, 0);
  g_assert_cmpint(g_rmdir(relative_dir), ==, 0);
  g_assert_cmpint(g_rmdir(second_dir), ==, 0);
  g_assert_cmpint(g_rmdir(first_dir), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_load_parse_table(void) {
  static const struct {
    const char *name;
    const char *contents;
    int32_t present;
    ShaulaPreviousAreaGeometry geometry;
  } cases[] = {
      {"plain", "1|2|3|4\n", 1, {1, 2, 3, 4}},
      {"whole-file-trim", " \t\r\n-1|+2|3|4\r\n ", 1, {-1, 2, 3, 4}},
      {"limits-and-underscores",
       "-2_147_483_648|2_147_483_647|4_294_967_295|1",
       1,
       {INT32_MIN, INT32_MAX, UINT32_MAX, 1}},
      {"consecutive-underscores", "1__0|-0|3|4", 1, {10, 0, 3, 4}},
      {"empty", "", 0, {0, 0, 0, 0}},
      {"whitespace", " \t\r\n", 0, {0, 0, 0, 0}},
      {"too-few", "1|2|3", 0, {0, 0, 0, 0}},
      {"too-many", "1|2|3|4|5", 0, {0, 0, 0, 0}},
      {"zero-width", "1|2|0|4", 0, {0, 0, 0, 0}},
      {"negative-zero-width", "1|2|-0|4", 0, {0, 0, 0, 0}},
      {"zero-height", "1|2|3|+0", 0, {0, 0, 0, 0}},
      {"x-overflow", "2147483648|2|3|4", 0, {0, 0, 0, 0}},
      {"x-underflow", "-2147483649|2|3|4", 0, {0, 0, 0, 0}},
      {"width-overflow", "1|2|4294967296|4", 0, {0, 0, 0, 0}},
      {"field-whitespace", "1| 2|3|4", 0, {0, 0, 0, 0}},
      {"leading-underscore", "_1|2|3|4", 0, {0, 0, 0, 0}},
      {"trailing-underscore", "1|2_|3|4", 0, {0, 0, 0, 0}},
      {"bare-sign", "+|2|3|4", 0, {0, 0, 0, 0}},
      {"nondecimal", "0x1|2|3|4", 0, {0, 0, 0, 0}},
      {"extra-line", "1|2|3|4\n5", 0, {0, 0, 0, 0}},
  };
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-previous-area-load-XXXXXX", &error);
  g_autofree char *path = NULL;
  size_t index;

  g_assert_no_error(error);
  g_assert_nonnull(root);
  path = g_build_filename(root, "state", NULL);

  for (index = 0; index < G_N_ELEMENTS(cases); index += 1) {
    int32_t present = 7;
    ShaulaPreviousAreaGeometry loaded = {9, 9, 9, 9};
    ShaulaPreviousAreaStatus status;

    g_test_message("case: %s", cases[index].name);
    g_assert_true(g_file_set_contents(path, cases[index].contents,
                                      (gssize)strlen(cases[index].contents),
                                      &error));
    g_assert_no_error(error);

    status =
        shaula_previous_area_load(span_from_string(path), &present, &loaded);
    g_assert_cmpint(status, ==, SHAULA_PREVIOUS_AREA_STATUS_OK);
    g_assert_cmpint(present, ==, cases[index].present);
    if (present) {
      assert_geometry(loaded, cases[index].geometry);
    } else {
      assert_geometry(loaded, (ShaulaPreviousAreaGeometry){0});
    }
  }

  {
    static const char embedded_nul[] = {'1', '|', '2',  '|', '3',
                                        '|', '4', '\0', '5'};
    int32_t present = 7;
    ShaulaPreviousAreaGeometry loaded = {9, 9, 9, 9};

    g_assert_true(
        g_file_set_contents(path, embedded_nul, sizeof(embedded_nul), &error));
    g_assert_no_error(error);
    g_assert_cmpint(
        shaula_previous_area_load(span_from_string(path), &present, &loaded),
        ==, SHAULA_PREVIOUS_AREA_STATUS_OK);
    g_assert_cmpint(present, ==, 0);
    assert_geometry(loaded, (ShaulaPreviousAreaGeometry){0});
  }

  g_assert_cmpint(g_remove(path), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_missing_and_error_contract(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-previous-area-errors-XXXXXX", &error);
  g_autofree char *missing = NULL;
  int32_t present = 7;
  ShaulaPreviousAreaGeometry geometry = {9, 9, 9, 9};
  ShaulaPreviousAreaGeometry value = {1, 2, 3, 4};
  static const char embedded_path[] = {'a', '\0', 'b'};

  g_assert_no_error(error);
  g_assert_nonnull(root);
  missing = g_build_filename(root, "missing", NULL);

  g_assert_cmpint(
      shaula_previous_area_load(span_from_string(missing), &present, &geometry),
      ==, SHAULA_PREVIOUS_AREA_STATUS_OK);
  g_assert_cmpint(present, ==, 0);
  assert_geometry(geometry, (ShaulaPreviousAreaGeometry){0});

  present = 7;
  geometry = (ShaulaPreviousAreaGeometry){9, 9, 9, 9};
  g_assert_cmpint(
      shaula_previous_area_load(span_from_string(root), &present, &geometry),
      ==, SHAULA_PREVIOUS_AREA_STATUS_OK);
  g_assert_cmpint(present, ==, 0);
  assert_geometry(geometry, (ShaulaPreviousAreaGeometry){0});

  g_assert_cmpint(
      shaula_previous_area_store(
          (ShaulaPreviousAreaSpan){embedded_path, sizeof(embedded_path)},
          value),
      ==, SHAULA_PREVIOUS_AREA_STATUS_FILESYSTEM_ERROR);
  present = 7;
  g_assert_cmpint(
      shaula_previous_area_load(
          (ShaulaPreviousAreaSpan){embedded_path, sizeof(embedded_path)},
          &present, &geometry),
      ==, SHAULA_PREVIOUS_AREA_STATUS_OK);
  g_assert_cmpint(present, ==, 0);

  g_assert_cmpint(shaula_previous_area_store(
                      (ShaulaPreviousAreaSpan){"x", SIZE_MAX}, value),
                  ==, SHAULA_PREVIOUS_AREA_STATUS_OUT_OF_MEMORY);
  present = 7;
  g_assert_cmpint(
      shaula_previous_area_load((ShaulaPreviousAreaSpan){"x", SIZE_MAX},
                                &present, &geometry),
      ==, SHAULA_PREVIOUS_AREA_STATUS_OK);
  g_assert_cmpint(present, ==, 0);

  g_assert_cmpint(
      shaula_previous_area_store((ShaulaPreviousAreaSpan){NULL, 1}, value), ==,
      SHAULA_PREVIOUS_AREA_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(shaula_previous_area_load((ShaulaPreviousAreaSpan){NULL, 1},
                                            &present, &geometry),
                  ==, SHAULA_PREVIOUS_AREA_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(
      shaula_previous_area_load(span_from_string(missing), NULL, &geometry), ==,
      SHAULA_PREVIOUS_AREA_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(
      shaula_previous_area_load(span_from_string(missing), &present, NULL), ==,
      SHAULA_PREVIOUS_AREA_STATUS_INVALID_ARGUMENT);

  g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_backend_support(void) {
  static const struct {
    const char *label;
    int32_t expected;
  } cases[] = {
      {"portal-screenshot", 0},
      {"portal-screenshot ", 1},
      {"Portal-Screenshot", 1},
      {"", 1},
      {"grim", 1},
      {"\xE6\x96\x87", 1},
  };
  size_t index;

  for (index = 0; index < G_N_ELEMENTS(cases); index += 1) {
    g_assert_cmpint(shaula_previous_area_supported_for_backend(
                        span_from_string(cases[index].label)),
                    ==, cases[index].expected);
  }
  g_assert_cmpint(shaula_previous_area_supported_for_backend(
                      (ShaulaPreviousAreaSpan){NULL, 1}),
                  ==, 1);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/runtime/previous-area/store-round-trip",
                  test_store_exact_format_and_round_trip);
  g_test_add_func("/runtime/previous-area/load-table", test_load_parse_table);
  g_test_add_func("/runtime/previous-area/errors",
                  test_missing_and_error_contract);
  g_test_add_func("/runtime/previous-area/backend-support",
                  test_backend_support);
  return g_test_run();
}
