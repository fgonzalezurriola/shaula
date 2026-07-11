#include "preview_result.h"

#include <glib.h>
#include <stdint.h>
#include <string.h>

static ShaulaPreviewResultSpan span_from_text(const char *text) {
  return (ShaulaPreviewResultSpan){(const uint8_t *)text, strlen(text)};
}

static void assert_default_result(const ShaulaPreviewResult *result) {
  g_assert_cmpint(result->closed, ==, 0);
  g_assert_cmpint(result->action, ==, SHAULA_PREVIEW_ACTION_UNKNOWN);
  g_assert_cmpint(result->copied, ==, 0);
  g_assert_cmpint(result->saved, ==, 0);
  g_assert_cmpint(result->notified, ==, 0);
  g_assert_null(result->saved_path.data);
  g_assert_cmpuint(result->saved_path.length, ==, 0);
}

static void test_action_abi_and_tokens(void) {
  g_assert_cmpint(SHAULA_PREVIEW_ACTION_CLOSE, ==, 0);
  g_assert_cmpint(SHAULA_PREVIEW_ACTION_COPY, ==, 1);
  g_assert_cmpint(SHAULA_PREVIEW_ACTION_SAVE, ==, 2);
  g_assert_cmpint(SHAULA_PREVIEW_ACTION_DISCARD, ==, 3);
  g_assert_cmpint(SHAULA_PREVIEW_ACTION_UNKNOWN, ==, 4);

  const struct {
    ShaulaPreviewAction action;
    const char *token;
  } cases[] = {
      {SHAULA_PREVIEW_ACTION_CLOSE, "close"},
      {SHAULA_PREVIEW_ACTION_COPY, "copy"},
      {SHAULA_PREVIEW_ACTION_SAVE, "save"},
      {SHAULA_PREVIEW_ACTION_DISCARD, "discard"},
      {SHAULA_PREVIEW_ACTION_UNKNOWN, "unknown"},
  };

  for (size_t index = 0; index < G_N_ELEMENTS(cases); index += 1) {
    const ShaulaPreviewResultSpan token =
        shaula_preview_action_token(cases[index].action);
    g_assert_nonnull(token.data);
    g_assert_cmpuint(token.length, ==, strlen(cases[index].token));
    g_assert_cmpmem(token.data, token.length, cases[index].token,
                    strlen(cases[index].token));
  }

  const ShaulaPreviewResultSpan invalid = shaula_preview_action_token(99);
  g_assert_null(invalid.data);
  g_assert_cmpuint(invalid.length, ==, 0);
}

static void test_successful_helper_payloads(void) {
  const struct {
    const char *payload;
    ShaulaPreviewAction action;
    int32_t copied;
    int32_t saved;
  } cases[] = {
      {"{\"closed\":true,\"action\":\"close\",\"copied\":false,"
       "\"saved\":false}",
       SHAULA_PREVIEW_ACTION_CLOSE, 0, 0},
      {"{\"closed\":true,\"action\":\"copy\",\"copied\":true,"
       "\"saved\":false,\"saved_path\":null}",
       SHAULA_PREVIEW_ACTION_COPY, 1, 0},
      {"{\"closed\":true,\"action\":\"save\",\"copied\":false,"
       "\"saved\":true,\"saved_path\":\"/tmp/b.png\"}\n",
       SHAULA_PREVIEW_ACTION_SAVE, 0, 1},
      {"{\"closed\":true,\"action\":\"discard\",\"copied\":false,"
       "\"saved\":false}",
       SHAULA_PREVIEW_ACTION_DISCARD, 0, 0},
      {"{\"closed\":true,\"action\":\"pin\"}",
       SHAULA_PREVIEW_ACTION_UNKNOWN, 0, 0},
  };

  for (size_t index = 0; index < G_N_ELEMENTS(cases); index += 1) {
    ShaulaPreviewResult result;
    shaula_preview_result_init(&result);
    g_assert_cmpint(shaula_preview_result_parse(span_from_text(cases[index].payload),
                                                &result),
                    ==, SHAULA_PREVIEW_RESULT_STATUS_OK);
    g_assert_cmpint(result.closed, ==, 1);
    g_assert_cmpint(result.action, ==, cases[index].action);
    g_assert_cmpint(result.copied, ==, cases[index].copied);
    g_assert_cmpint(result.saved, ==, cases[index].saved);
    if (cases[index].action == SHAULA_PREVIEW_ACTION_SAVE) {
      g_assert_cmpuint(result.saved_path.length, ==, strlen("/tmp/b.png"));
      g_assert_cmpmem(result.saved_path.data, result.saved_path.length,
                      "/tmp/b.png", strlen("/tmp/b.png"));
      g_assert_cmpuint(result.saved_path.data[result.saved_path.length], ==, 0);
    } else {
      g_assert_null(result.saved_path.data);
    }
    shaula_preview_result_clear(&result);
    assert_default_result(&result);
  }
}

static void test_missing_and_non_object_payloads(void) {
  const char *missing[] = {"", " \t\r\n"};
  for (size_t index = 0; index < G_N_ELEMENTS(missing); index += 1) {
    ShaulaPreviewResult result;
    shaula_preview_result_init(&result);
    g_assert_cmpint(shaula_preview_result_parse(span_from_text(missing[index]),
                                                &result),
                    ==, SHAULA_PREVIEW_RESULT_STATUS_MISSING);
    assert_default_result(&result);
  }

  const char *invalid_roots[] = {"null", "true", "1", "\"value\"", "[]"};
  for (size_t index = 0; index < G_N_ELEMENTS(invalid_roots); index += 1) {
    ShaulaPreviewResult result;
    shaula_preview_result_init(&result);
    g_assert_cmpint(shaula_preview_result_parse(
                        span_from_text(invalid_roots[index]), &result),
                    ==, SHAULA_PREVIEW_RESULT_STATUS_INVALID_JSON);
    assert_default_result(&result);
  }
}

static void test_defaults_and_wrong_types(void) {
  ShaulaPreviewResult result;
  shaula_preview_result_init(&result);
  g_assert_cmpint(shaula_preview_result_parse(span_from_text("{}"), &result),
                  ==, SHAULA_PREVIEW_RESULT_STATUS_OK);
  assert_default_result(&result);

  g_assert_cmpint(
      shaula_preview_result_parse(
          span_from_text("{\"closed\":1,\"action\":false,\"copied\":null,"
                         "\"saved\":[],\"notified\":{},\"saved_path\":42}"),
          &result),
      ==, SHAULA_PREVIEW_RESULT_STATUS_OK);
  assert_default_result(&result);
  shaula_preview_result_clear(&result);
}

static void test_unknown_fields_and_complete_json_grammar(void) {
  ShaulaPreviewResult result;
  shaula_preview_result_init(&result);
  g_assert_cmpint(
      shaula_preview_result_parse(
          span_from_text("{\"unknown\":{\"nested\":[null,true,false,-1,2.5,"
                         "1e9999]},\"closed\":true,\"notified\":true}"),
          &result),
      ==, SHAULA_PREVIEW_RESULT_STATUS_OK);
  g_assert_cmpint(result.closed, ==, 1);
  g_assert_cmpint(result.notified, ==, 1);
  g_assert_cmpint(result.action, ==, SHAULA_PREVIEW_ACTION_UNKNOWN);
  shaula_preview_result_clear(&result);
}

static void test_duplicate_keys_are_invalid_everywhere(void) {
  const char *payloads[] = {
      "{\"closed\":true,\"closed\":false}",
      "{\"closed\":true,\"cl\\u006fsed\":false}",
      "{\"unknown\":1,\"unknown\":2}",
      "{\"unknown\":{\"x\":1,\"x\":2}}",
      "{\"unknown\":{\"x\\u0000y\":1,\"x\\u0000y\":2}}",
  };

  for (size_t index = 0; index < G_N_ELEMENTS(payloads); index += 1) {
    ShaulaPreviewResult result;
    shaula_preview_result_init(&result);
    g_assert_cmpint(shaula_preview_result_parse(span_from_text(payloads[index]),
                                                &result),
                    ==, SHAULA_PREVIEW_RESULT_STATUS_INVALID_JSON);
    assert_default_result(&result);
  }
}

static void test_escaped_keys_unicode_and_embedded_nul(void) {
  const char *payload =
      "{\"cl\\u006fsed\":true,\"action\":\"sa\\u0076e\","
      "\"notified\":true,\"saved_path\":"
      "\"a\\\\b\\n\\u00e9\\ud83d\\ude00\\u0000z\"}";
  const uint8_t expected[] = {'a', '\\', 'b', '\n', 0xc3, 0xa9, 0xf0,
                              0x9f, 0x98, 0x80, 0x00, 'z'};

  ShaulaPreviewResult result;
  shaula_preview_result_init(&result);
  g_assert_cmpint(shaula_preview_result_parse(span_from_text(payload), &result),
                  ==, SHAULA_PREVIEW_RESULT_STATUS_OK);
  g_assert_cmpint(result.closed, ==, 1);
  g_assert_cmpint(result.action, ==, SHAULA_PREVIEW_ACTION_SAVE);
  g_assert_cmpint(result.notified, ==, 1);
  g_assert_cmpuint(result.saved_path.length, ==, sizeof(expected));
  g_assert_cmpmem(result.saved_path.data, result.saved_path.length, expected,
                  sizeof(expected));
  g_assert_cmpuint(result.saved_path.data[result.saved_path.length], ==, 0);
  shaula_preview_result_clear(&result);
}

static void test_empty_nullable_path_and_unknown_action(void) {
  const char *saved_paths[] = {"null", "\"\"", "false", "[]", "{}"};
  for (size_t index = 0; index < G_N_ELEMENTS(saved_paths); index += 1) {
    gchar *payload = g_strdup_printf(
        "{\"action\":\"SAVE\",\"saved_path\":%s}", saved_paths[index]);
    ShaulaPreviewResult result;
    shaula_preview_result_init(&result);
    g_assert_cmpint(shaula_preview_result_parse(span_from_text(payload), &result),
                    ==, SHAULA_PREVIEW_RESULT_STATUS_OK);
    g_assert_cmpint(result.action, ==, SHAULA_PREVIEW_ACTION_UNKNOWN);
    g_assert_null(result.saved_path.data);
    shaula_preview_result_clear(&result);
    g_free(payload);
  }
}

static void test_trailing_data_and_invalid_json_grammar(void) {
  const char *payloads[] = {
      "{",          "{}{}",       "{} trailing", "{\"x\":01}",
      "{\"x\":1.}", "{\"x\":1e}", "{\"x\":}",   "{\"x\":true,}",
      "{\"x\":tru}", "[",          "\v{}",
  };

  for (size_t index = 0; index < G_N_ELEMENTS(payloads); index += 1) {
    ShaulaPreviewResult result;
    shaula_preview_result_init(&result);
    g_assert_cmpint(shaula_preview_result_parse(span_from_text(payloads[index]),
                                                &result),
                    ==, SHAULA_PREVIEW_RESULT_STATUS_INVALID_JSON);
    assert_default_result(&result);
  }
}

static void test_invalid_string_encodings(void) {
  const char *payloads[] = {
      "{\"saved_path\":\"\\ud800\"}",
      "{\"saved_path\":\"\\udc00\"}",
      "{\"saved_path\":\"\\ud800x\"}",
      "{\"saved_path\":\"\\u12xz\"}",
      "{\"saved_path\":\"\\q\"}",
      "{\"saved_path\":\"line\nbreak\"}",
  };

  for (size_t index = 0; index < G_N_ELEMENTS(payloads); index += 1) {
    ShaulaPreviewResult result;
    shaula_preview_result_init(&result);
    g_assert_cmpint(shaula_preview_result_parse(span_from_text(payloads[index]),
                                                &result),
                    ==, SHAULA_PREVIEW_RESULT_STATUS_INVALID_JSON);
    assert_default_result(&result);
  }

  const uint8_t invalid_utf8[] = {'{', '"', 'x', '"', ':', '"', 0xff,
                                  '"', '}'};
  ShaulaPreviewResult result;
  shaula_preview_result_init(&result);
  g_assert_cmpint(
      shaula_preview_result_parse(
          (ShaulaPreviewResultSpan){invalid_utf8, sizeof(invalid_utf8)}, &result),
      ==, SHAULA_PREVIEW_RESULT_STATUS_INVALID_JSON);
  assert_default_result(&result);
}

static void test_embedded_nul_and_invalid_arguments(void) {
  const uint8_t raw_nul[] = {'{', '}', 0};
  ShaulaPreviewResult result;
  shaula_preview_result_init(&result);
  g_assert_cmpint(
      shaula_preview_result_parse(
          (ShaulaPreviewResultSpan){raw_nul, sizeof(raw_nul)}, &result),
      ==, SHAULA_PREVIEW_RESULT_STATUS_INVALID_JSON);
  assert_default_result(&result);

  g_assert_cmpint(shaula_preview_result_parse(
                      (ShaulaPreviewResultSpan){NULL, 1}, &result),
                  ==, SHAULA_PREVIEW_RESULT_STATUS_INVALID_ARGUMENT);
  assert_default_result(&result);
  g_assert_cmpint(shaula_preview_result_parse(
                      (ShaulaPreviewResultSpan){NULL, 0}, &result),
                  ==, SHAULA_PREVIEW_RESULT_STATUS_MISSING);
  assert_default_result(&result);
  const uint8_t oversized_marker = '{';
  g_assert_cmpint(
      shaula_preview_result_parse(
          (ShaulaPreviewResultSpan){&oversized_marker, G_MAXSIZE}, &result),
      ==, SHAULA_PREVIEW_RESULT_STATUS_OUT_OF_MEMORY);
  assert_default_result(&result);
  g_assert_cmpint(shaula_preview_result_parse(span_from_text("{}"), NULL), ==,
                  SHAULA_PREVIEW_RESULT_STATUS_INVALID_ARGUMENT);
}

static void test_reparse_releases_owned_output(void) {
  ShaulaPreviewResult result;
  shaula_preview_result_init(&result);
  g_assert_cmpint(
      shaula_preview_result_parse(
          span_from_text("{\"saved_path\":\"first.png\"}"), &result),
      ==, SHAULA_PREVIEW_RESULT_STATUS_OK);
  g_assert_nonnull(result.saved_path.data);

  g_assert_cmpint(shaula_preview_result_parse(span_from_text("{}"), &result),
                  ==, SHAULA_PREVIEW_RESULT_STATUS_OK);
  assert_default_result(&result);
  shaula_preview_result_clear(&result);
  shaula_preview_result_clear(&result);
  assert_default_result(&result);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/preview-result/action-abi-and-tokens",
                  test_action_abi_and_tokens);
  g_test_add_func("/preview-result/successful-helper-payloads",
                  test_successful_helper_payloads);
  g_test_add_func("/preview-result/missing-and-non-object",
                  test_missing_and_non_object_payloads);
  g_test_add_func("/preview-result/defaults-and-wrong-types",
                  test_defaults_and_wrong_types);
  g_test_add_func("/preview-result/unknown-fields-and-json-grammar",
                  test_unknown_fields_and_complete_json_grammar);
  g_test_add_func("/preview-result/duplicate-keys",
                  test_duplicate_keys_are_invalid_everywhere);
  g_test_add_func("/preview-result/escapes-unicode-and-nul",
                  test_escaped_keys_unicode_and_embedded_nul);
  g_test_add_func("/preview-result/empty-path-and-unknown-action",
                  test_empty_nullable_path_and_unknown_action);
  g_test_add_func("/preview-result/trailing-and-invalid-grammar",
                  test_trailing_data_and_invalid_json_grammar);
  g_test_add_func("/preview-result/invalid-string-encodings",
                  test_invalid_string_encodings);
  g_test_add_func("/preview-result/embedded-nul-and-invalid-arguments",
                  test_embedded_nul_and_invalid_arguments);
  g_test_add_func("/preview-result/reparse-and-clear",
                  test_reparse_releases_owned_output);
  return g_test_run();
}
