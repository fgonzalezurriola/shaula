#include "env.h"

#include <glib.h>
#include <stdint.h>
#include <string.h>

static void assert_span(ShaulaEnvSpan actual, const char *expected,
                        size_t expected_length) {
  g_assert_nonnull(actual.data);
  g_assert_cmpuint(actual.length, ==, expected_length);
  g_assert_cmpmem(actual.data, actual.length, expected, expected_length);
}

static void test_slice_and_trimmed_values(void) {
  static const struct {
    const char *name;
    const char *value;
    ShaulaEnvStatus slice_status;
    const char *slice_expected;
    size_t slice_length;
    ShaulaEnvStatus trimmed_status;
    const char *trimmed_expected;
    size_t trimmed_length;
    size_t trimmed_offset;
  } cases[] = {
      {"missing", NULL, SHAULA_ENV_STATUS_MISSING, NULL, 0,
       SHAULA_ENV_STATUS_MISSING, NULL, 0, 0},
      {"empty", "", SHAULA_ENV_STATUS_VALID, "", 0, SHAULA_ENV_STATUS_MISSING,
       NULL, 0, 0},
      {"whitespace-only", " \t\r\n", SHAULA_ENV_STATUS_VALID, " \t\r\n", 4,
       SHAULA_ENV_STATUS_MISSING, NULL, 0, 0},
      {"plain", "value", SHAULA_ENV_STATUS_VALID, "value", 5,
       SHAULA_ENV_STATUS_VALID, "value", 5, 0},
      {"leading-trailing", " \tvalue\r\n", SHAULA_ENV_STATUS_VALID,
       " \tvalue\r\n", 9, SHAULA_ENV_STATUS_VALID, "value", 5, 2},
      {"non-ascii", " \xC3\xA9 ", SHAULA_ENV_STATUS_VALID, " \xC3\xA9 ", 4,
       SHAULA_ENV_STATUS_VALID, "\xC3\xA9", 2, 1},
  };
  size_t index;

  for (index = 0; index < G_N_ELEMENTS(cases); index += 1) {
    ShaulaEnvSpan sliced = {"sentinel", 99};
    ShaulaEnvSpan trimmed = {"sentinel", 99};
    ShaulaEnvStatus slice_status;
    ShaulaEnvStatus trimmed_status;

    g_test_message("case: %s", cases[index].name);
    slice_status = shaula_env_value_slice(cases[index].value, &sliced);
    trimmed_status = shaula_env_value_trimmed(cases[index].value, &trimmed);

    g_assert_cmpint(slice_status, ==, cases[index].slice_status);
    g_assert_cmpint(trimmed_status, ==, cases[index].trimmed_status);

    if (slice_status == SHAULA_ENV_STATUS_VALID) {
      assert_span(sliced, cases[index].slice_expected,
                  cases[index].slice_length);
      g_assert_true(sliced.data == cases[index].value);
    } else {
      g_assert_null(sliced.data);
      g_assert_cmpuint(sliced.length, ==, 0);
    }

    if (trimmed_status == SHAULA_ENV_STATUS_VALID) {
      assert_span(trimmed, cases[index].trimmed_expected,
                  cases[index].trimmed_length);
      g_assert_true(trimmed.data ==
                    cases[index].value + cases[index].trimmed_offset);
    } else {
      g_assert_null(trimmed.data);
      g_assert_cmpuint(trimmed.length, ==, 0);
    }
  }
}

static void test_borrowed_lifetime_across_calls(void) {
  const char *first_value = "first";
  const char *second_value = "second";
  ShaulaEnvSpan first;
  ShaulaEnvSpan second;

  g_assert_cmpint(shaula_env_value_slice(first_value, &first), ==,
                  SHAULA_ENV_STATUS_VALID);
  g_assert_cmpint(shaula_env_value_slice(second_value, &second), ==,
                  SHAULA_ENV_STATUS_VALID);

  assert_span(first, "first", 5);
  assert_span(second, "second", 6);
  g_assert_true(first.data == first_value);
  g_assert_true(second.data == second_value);
}

static void test_boolean_values(void) {
  static const struct {
    const char *name;
    const char *value;
    ShaulaEnvStatus status;
    int32_t expected;
  } cases[] = {
      {"missing", NULL, SHAULA_ENV_STATUS_MISSING, 0},
      {"empty", "", SHAULA_ENV_STATUS_MISSING, 0},
      {"whitespace", " \t\r\n", SHAULA_ENV_STATUS_MISSING, 0},
      {"one", "1", SHAULA_ENV_STATUS_VALID, 1},
      {"true", "true", SHAULA_ENV_STATUS_VALID, 1},
      {"true-mixed-case", "TrUe", SHAULA_ENV_STATUS_VALID, 1},
      {"yes-upper", "YES", SHAULA_ENV_STATUS_VALID, 1},
      {"zero", "0", SHAULA_ENV_STATUS_VALID, 0},
      {"false", "false", SHAULA_ENV_STATUS_VALID, 0},
      {"false-mixed-case", "FaLsE", SHAULA_ENV_STATUS_VALID, 0},
      {"no-upper", "NO", SHAULA_ENV_STATUS_VALID, 0},
      {"trimmed-true", " \t yes\r\n", SHAULA_ENV_STATUS_VALID, 1},
      {"trimmed-false", " no ", SHAULA_ENV_STATUS_VALID, 0},
      {"malformed-on", "on", SHAULA_ENV_STATUS_INVALID, 0},
      {"malformed-number", "2", SHAULA_ENV_STATUS_INVALID, 0},
      {"malformed-plus", "+1", SHAULA_ENV_STATUS_INVALID, 0},
      {"malformed-junk", "truex", SHAULA_ENV_STATUS_INVALID, 0},
      {"non-ascii", "\xC3\xA9", SHAULA_ENV_STATUS_INVALID, 0},
  };
  size_t index;

  for (index = 0; index < G_N_ELEMENTS(cases); index += 1) {
    int32_t parsed = 99;
    ShaulaEnvStatus status;

    g_test_message("case: %s", cases[index].name);
    status = shaula_env_value_flag(cases[index].value, &parsed);
    g_assert_cmpint(status, ==, cases[index].status);
    g_assert_cmpint(parsed, ==, cases[index].expected);
  }
}

static void test_unsigned_values(void) {
  static const struct {
    const char *name;
    const char *value;
    uint64_t max_value;
    uint64_t default_value;
    uint64_t expected;
  } cases[] = {
      {"missing", NULL, UINT64_MAX, 77, 77},
      {"empty", "", UINT64_MAX, 77, 77},
      {"whitespace-only", " \t\r\n", UINT64_MAX, 77, 77},
      {"zero", "0", UINT64_MAX, 77, 0},
      {"normal", "12345", UINT64_MAX, 77, 12345},
      {"exact-u64-max", "18446744073709551615", UINT64_MAX, 77, UINT64_MAX},
      {"one-past-u64-max", "18446744073709551616", UINT64_MAX, 77, 77},
      {"negative", "-1", UINT64_MAX, 77, 77},
      {"negative-zero", "-0", UINT64_MAX, 77, 0},
      {"leading-plus", "+42", UINT64_MAX, 77, 42},
      {"leading-trailing-whitespace", " \t42\r\n", UINT64_MAX, 77, 42},
      {"trailing-junk", "42ms", UINT64_MAX, 77, 77},
      {"internal-underscore", "1_000", UINT64_MAX, 77, 1000},
      {"repeated-underscore", "1__2", UINT64_MAX, 77, 12},
      {"leading-underscore", "_12", UINT64_MAX, 77, 77},
      {"trailing-underscore", "12_", UINT64_MAX, 77, 77},
      {"sign-only", "+", UINT64_MAX, 77, 77},
      {"decimal-only", "0x10", UINT64_MAX, 77, 77},
      {"exact-u8-max", "255", UINT8_MAX, 77, 255},
      {"one-past-u8-max", "256", UINT8_MAX, 77, 77},
      {"non-ascii", "\xC3\xA9", UINT64_MAX, 77, 77},
  };
  size_t index;

  for (index = 0; index < G_N_ELEMENTS(cases); index += 1) {
    uint64_t actual;

    g_test_message("case: %s", cases[index].name);
    actual = shaula_env_value_unsigned_or_default(
        cases[index].value, cases[index].max_value, cases[index].default_value);
    g_assert_true(actual == cases[index].expected);
  }
}

static void test_desktop_tokens(void) {
  static const struct {
    const char *name;
    const char *value;
    ShaulaEnvStatus status;
    const char *expected;
    size_t expected_length;
    size_t expected_offset;
  } cases[] = {
      {"missing", NULL, SHAULA_ENV_STATUS_MISSING, NULL, 0, 0},
      {"empty", "", SHAULA_ENV_STATUS_MISSING, NULL, 0, 0},
      {"only-separators", "::;;;", SHAULA_ENV_STATUS_MISSING, NULL, 0, 0},
      {"only-empty-whitespace", " : \t;\r\n:", SHAULA_ENV_STATUS_MISSING, NULL,
       0, 0},
      {"plain", "GNOME", SHAULA_ENV_STATUS_VALID, "GNOME", 5, 0},
      {"colon", "GNOME:KDE", SHAULA_ENV_STATUS_VALID, "GNOME", 5, 0},
      {"semicolon", "GNOME;KDE", SHAULA_ENV_STATUS_VALID, "GNOME", 5, 0},
      {"repeated-separators", "::; sway ;;:GNOME", SHAULA_ENV_STATUS_VALID,
       "sway", 4, 4},
      {"trimmed", " \tPlasma\r\n ; KDE", SHAULA_ENV_STATUS_VALID, "Plasma", 6,
       2},
      {"case-preserved", "nIrI:GNOME", SHAULA_ENV_STATUS_VALID, "nIrI", 4, 0},
      {"non-ascii", "\xC3\xA9:kde", SHAULA_ENV_STATUS_VALID, "\xC3\xA9", 2, 0},
      {"no-substring-matching", "xgnome:gnome", SHAULA_ENV_STATUS_VALID,
       "xgnome", 6, 0},
  };
  size_t index;

  for (index = 0; index < G_N_ELEMENTS(cases); index += 1) {
    ShaulaEnvSpan input = {
        cases[index].value,
        cases[index].value == NULL ? 0 : strlen(cases[index].value)};
    ShaulaEnvSpan token = {"sentinel", 99};
    ShaulaEnvStatus status;

    g_test_message("case: %s", cases[index].name);
    status = shaula_env_first_desktop_token(input, &token);
    g_assert_cmpint(status, ==, cases[index].status);

    if (status == SHAULA_ENV_STATUS_VALID) {
      assert_span(token, cases[index].expected, cases[index].expected_length);
      g_assert_true(token.data ==
                    cases[index].value + cases[index].expected_offset);
    } else {
      g_assert_null(token.data);
      g_assert_cmpuint(token.length, ==, 0);
    }
  }

  {
    const char raw[] = "alpha:ignored";
    ShaulaEnvSpan token;
    ShaulaEnvStatus status =
        shaula_env_first_desktop_token((ShaulaEnvSpan){raw, 5}, &token);
    g_assert_cmpint(status, ==, SHAULA_ENV_STATUS_VALID);
    assert_span(token, "alpha", 5);
  }
}

static void test_null_output_contract(void) {
  int32_t parsed = 0;

  g_assert_cmpint(shaula_env_value_slice("value", NULL), ==,
                  SHAULA_ENV_STATUS_INVALID);
  g_assert_cmpint(shaula_env_value_trimmed("value", NULL), ==,
                  SHAULA_ENV_STATUS_INVALID);
  g_assert_cmpint(shaula_env_value_flag("true", NULL), ==,
                  SHAULA_ENV_STATUS_INVALID);
  g_assert_cmpint(
      shaula_env_first_desktop_token((ShaulaEnvSpan){"GNOME", 5}, NULL), ==,
      SHAULA_ENV_STATUS_INVALID);
  g_assert_cmpint(shaula_env_value_flag(NULL, &parsed), ==,
                  SHAULA_ENV_STATUS_MISSING);
  g_assert_cmpint(parsed, ==, 0);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/runtime/env/string-values", test_slice_and_trimmed_values);
  g_test_add_func("/runtime/env/borrowed-lifetime",
                  test_borrowed_lifetime_across_calls);
  g_test_add_func("/runtime/env/boolean", test_boolean_values);
  g_test_add_func("/runtime/env/unsigned", test_unsigned_values);
  g_test_add_func("/runtime/env/desktop-token", test_desktop_tokens);
  g_test_add_func("/runtime/env/null-output", test_null_output_contract);
  return g_test_run();
}
