#include "notify/request.h"

#include <glib.h>
#include <stdint.h>
#include <string.h>

static ShaulaNotifySpan span_from_bytes(const void *data, size_t length) {
  return (ShaulaNotifySpan){data, length};
}

static ShaulaNotifySpan span_from_string(const char *value) {
  return span_from_bytes(value, strlen(value));
}

static void assert_span_bytes(ShaulaNotifySpan actual,
                              const void *expected,
                              size_t expected_length) {
  g_assert_cmpuint(actual.length, ==, expected_length);
  if (expected_length == 0U) {
    return;
  }
  g_assert_nonnull(actual.data);
  g_assert_cmpmem(actual.data, actual.length, expected, expected_length);
}

static void assert_span_string(ShaulaNotifySpan actual, const char *expected) {
  assert_span_bytes(actual, expected, strlen(expected));
}

static void assert_owned_string(ShaulaNotifyOwnedBytes actual,
                                const char *expected) {
  g_assert_nonnull(actual.data);
  g_assert_cmpuint(actual.length, ==, strlen(expected));
  g_assert_cmpmem(actual.data, actual.length, expected, strlen(expected));
  g_assert_cmpuint(actual.data[actual.length], ==, 0U);
}

static void test_request_defaults_and_urgency_tokens(void) {
  ShaulaNotifyRequest request;
  ShaulaNotifySpan first;
  ShaulaNotifySpan second;
  ShaulaNotifySpan invalid;

  memset(&request, 0xff, sizeof(request));
  shaula_notify_request_init(&request);

  g_assert_null(request.summary.data);
  g_assert_cmpuint(request.summary.length, ==, 0U);
  g_assert_null(request.body.data);
  g_assert_cmpuint(request.body.length, ==, 0U);
  g_assert_cmpuint(request.has_image_path, ==, 0U);
  g_assert_cmpint(request.urgency, ==, SHAULA_NOTIFY_URGENCY_NORMAL);
  g_assert_cmpuint(request.timeout_ms, ==, 2500U);
  g_assert_cmpuint(request.transient, ==, 1U);
  g_assert_cmpuint(request.has_action, ==, 0U);

  g_assert_cmpint(SHAULA_NOTIFY_URGENCY_LOW, ==, 0);
  g_assert_cmpint(SHAULA_NOTIFY_URGENCY_NORMAL, ==, 1);
  g_assert_cmpint(SHAULA_NOTIFY_URGENCY_CRITICAL, ==, 2);
  g_assert_cmpint(SHAULA_NOTIFY_IMAGE_MODE_HINT, ==, 0);
  g_assert_cmpint(SHAULA_NOTIFY_IMAGE_MODE_ICON, ==, 1);
  g_assert_cmpint(SHAULA_NOTIFY_STATUS_OK, ==, 0);
  g_assert_cmpint(SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT, ==, 1);
  g_assert_cmpint(SHAULA_NOTIFY_STATUS_SIZE_OVERFLOW, ==, 2);
  g_assert_cmpint(SHAULA_NOTIFY_STATUS_OUT_OF_MEMORY, ==, 3);
  g_assert_cmpint(SHAULA_NOTIFY_STATUS_INVALID_URGENCY, ==, 4);
  g_assert_cmpint(SHAULA_NOTIFY_STATUS_INVALID_IMAGE_MODE, ==, 5);
  g_assert_cmpint(SHAULA_NOTIFY_SEND_ARG_CAPACITY, ==, 16);

  assert_span_string(
      shaula_notify_urgency_token(SHAULA_NOTIFY_URGENCY_LOW), "low");
  assert_span_string(
      shaula_notify_urgency_token(SHAULA_NOTIFY_URGENCY_NORMAL), "normal");
  assert_span_string(
      shaula_notify_urgency_token(SHAULA_NOTIFY_URGENCY_CRITICAL),
      "critical");

  first = shaula_notify_urgency_token(SHAULA_NOTIFY_URGENCY_NORMAL);
  second = shaula_notify_urgency_token(SHAULA_NOTIFY_URGENCY_NORMAL);
  g_assert_true(first.data == second.data);
  invalid = shaula_notify_urgency_token(SHAULA_NOTIFY_URGENCY_INVALID);
  g_assert_null(invalid.data);
  g_assert_cmpuint(invalid.length, ==, 0U);
  invalid = shaula_notify_urgency_token(3);
  g_assert_null(invalid.data);
  g_assert_cmpuint(invalid.length, ==, 0U);

  shaula_notify_request_init(NULL);
}

static void test_default_argv_order_and_ownership(void) {
  static const uint8_t summary[] = "Screenshot captured";
  static const uint8_t body[] = "Copied.";
  ShaulaNotifyRequest request;
  ShaulaNotifySendArgs args = {0};

  shaula_notify_request_init(&request);
  request.summary = span_from_bytes(summary, sizeof(summary) - 1U);
  request.body = span_from_bytes(body, sizeof(body) - 1U);

  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_HINT,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_OK);
  g_assert_cmpuint(args.length, ==, 9U);
  assert_span_string(args.items[0], "notify-send");
  assert_span_string(args.items[1], "--app-name=Shaula");
  assert_span_string(args.items[2], "--urgency");
  assert_span_string(args.items[3], "normal");
  assert_span_string(args.items[4], "--expire-time");
  assert_span_string(args.items[5], "2500");
  assert_span_string(args.items[6], "--transient");
  assert_span_bytes(args.items[7], summary, sizeof(summary) - 1U);
  assert_span_bytes(args.items[8], body, sizeof(body) - 1U);
  g_assert_true(args.items[7].data == summary);
  g_assert_true(args.items[8].data == body);
  assert_owned_string(args.timeout, "2500");
  g_assert_null(args.image_hint.data);
  g_assert_cmpuint(args.image_hint.length, ==, 0U);
  g_assert_null(args.action_arg.data);
  g_assert_cmpuint(args.action_arg.length, ==, 0U);

  shaula_notify_send_args_clear(&args);
  g_assert_cmpuint(args.length, ==, 0U);
  g_assert_null(args.timeout.data);
  g_assert_null(args.image_hint.data);
  g_assert_null(args.action_arg.data);
  shaula_notify_send_args_clear(&args);
  shaula_notify_send_args_init(&args);
  shaula_notify_send_args_init(NULL);
  shaula_notify_send_args_clear(NULL);
}

static void test_hint_argv_with_action_and_max_timeout(void) {
  ShaulaNotifyRequest request;
  ShaulaNotifySendArgs args = {0};

  shaula_notify_request_init(&request);
  request.summary = span_from_string("Screenshot captured");
  request.body = span_from_string("Saved.");
  request.has_image_path = 1U;
  request.image_path = span_from_string("/tmp/shaula/cap one#%.png");
  request.urgency = SHAULA_NOTIFY_URGENCY_CRITICAL;
  request.timeout_ms = UINT32_MAX;
  request.has_action = 1U;
  request.action_id = span_from_string("default");
  request.action_label = span_from_string("Show in folder");

  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_HINT,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_OK);
  g_assert_cmpuint(args.length, ==, 12U);
  assert_span_string(args.items[3], "critical");
  assert_span_string(args.items[5], "4294967295");
  assert_span_string(args.items[6], "--transient");
  assert_span_string(args.items[7], "--hint");
  assert_span_string(
      args.items[8],
      "string:image-path:file:///tmp/shaula/cap%20one%23%25.png");
  assert_span_string(args.items[9], "--action=default=Show in folder");
  assert_span_string(args.items[10], "Screenshot captured");
  assert_span_string(args.items[11], "Saved.");
  assert_owned_string(args.timeout, "4294967295");
  assert_owned_string(
      args.image_hint,
      "string:image-path:file:///tmp/shaula/cap%20one%23%25.png");
  assert_owned_string(args.action_arg, "--action=default=Show in folder");

  shaula_notify_send_args_clear(&args);
}

static void test_icon_fallback_and_nontransient_argv(void) {
  static const uint8_t path[] = "/tmp/shaula/cap.png";
  ShaulaNotifyRequest request;
  ShaulaNotifySendArgs args = {0};

  shaula_notify_request_init(&request);
  request.summary = span_from_string("Screenshot captured");
  request.body = span_from_string("Copied.");
  request.has_image_path = 1U;
  request.image_path = span_from_bytes(path, sizeof(path) - 1U);
  request.urgency = SHAULA_NOTIFY_URGENCY_LOW;
  request.timeout_ms = 0U;
  request.transient = 0U;

  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_ICON,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_OK);
  g_assert_cmpuint(args.length, ==, 10U);
  assert_span_string(args.items[3], "low");
  assert_span_string(args.items[5], "0");
  assert_span_string(args.items[6], "-i");
  assert_span_bytes(args.items[7], path, sizeof(path) - 1U);
  g_assert_true(args.items[7].data == path);
  assert_span_string(args.items[8], "Screenshot captured");
  assert_span_string(args.items[9], "Copied.");
  g_assert_null(args.image_hint.data);
  g_assert_null(args.action_arg.data);

  shaula_notify_send_args_clear(&args);
}

static void test_present_empty_optionals(void) {
  ShaulaNotifyRequest request;
  ShaulaNotifySendArgs args = {0};

  shaula_notify_request_init(&request);
  request.has_image_path = 1U;
  request.image_path = (ShaulaNotifySpan){NULL, 0U};
  request.has_action = 1U;
  request.action_id = (ShaulaNotifySpan){NULL, 0U};
  request.action_label = (ShaulaNotifySpan){NULL, 0U};

  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_HINT,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_OK);
  g_assert_cmpuint(args.length, ==, 12U);
  assert_span_string(args.items[7], "--hint");
  assert_span_string(args.items[8], "string:image-path:file://");
  assert_span_string(args.items[9], "--action==");
  assert_span_bytes(args.items[10], NULL, 0U);
  assert_span_bytes(args.items[11], NULL, 0U);

  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_ICON,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_OK);
  g_assert_cmpuint(args.length, ==, 12U);
  assert_span_string(args.items[7], "-i");
  assert_span_bytes(args.items[8], NULL, 0U);
  assert_span_string(args.items[9], "--action==");

  shaula_notify_send_args_clear(&args);
}

static void test_uri_byte_escaping(void) {
  static const uint8_t path[] = {'/', 'A', 'z', '0', '9', '-', '_', '.', '~',
                                 ' ', '#', '?', '%', '\\', 0x00U, 0x7fU,
                                 0x80U, 0xffU};
  ShaulaNotifyOwnedBytes output = {0};

  g_assert_cmpint(
      shaula_notify_file_uri_build(span_from_bytes(path, sizeof(path)), &output),
      ==, SHAULA_NOTIFY_STATUS_OK);
  assert_owned_string(
      output,
      "file:///Az09-_.~%20%23%3F%25%5C%00%7F%80%FF");

  g_assert_cmpint(
      shaula_notify_file_uri_build((ShaulaNotifySpan){NULL, 0U}, &output),
      ==, SHAULA_NOTIFY_STATUS_OK);
  assert_owned_string(output, "file://");

  g_assert_cmpint(
      shaula_notify_file_uri_build((ShaulaNotifySpan){NULL, 1U}, &output),
      ==, SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT);
  g_assert_null(output.data);
  g_assert_cmpuint(output.length, ==, 0U);

  g_assert_cmpint(
      shaula_notify_file_uri_build(
          (ShaulaNotifySpan){(const uint8_t *)"x", SIZE_MAX / 3U + 1U},
          &output),
      ==, SHAULA_NOTIFY_STATUS_SIZE_OVERFLOW);
  g_assert_null(output.data);
  g_assert_cmpuint(output.length, ==, 0U);

  shaula_notify_owned_bytes_clear(&output);
  shaula_notify_owned_bytes_clear(&output);
  shaula_notify_owned_bytes_init(&output);
  shaula_notify_owned_bytes_init(NULL);
  shaula_notify_owned_bytes_clear(NULL);
  g_assert_cmpint(
      shaula_notify_file_uri_build(span_from_string("/tmp/value"), NULL),
      ==, SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT);
}

static void test_embedded_nul_and_arbitrary_bytes(void) {
  static const uint8_t summary[] = {'A', 0x00U, 'B'};
  static const uint8_t body[] = {0xffU, 0x80U};
  static const uint8_t path[] = {'/', 'x', 0x00U, 0xffU};
  static const uint8_t action_id[] = {'i', 0x00U, 'd'};
  static const uint8_t action_label[] = {'L', 0xffU};
  static const uint8_t expected_action[] = {
      '-', '-', 'a', 'c', 't', 'i', 'o', 'n', '=',
      'i', 0x00U, 'd', '=', 'L', 0xffU};
  ShaulaNotifyRequest request;
  ShaulaNotifySendArgs args = {0};

  shaula_notify_request_init(&request);
  request.summary = span_from_bytes(summary, sizeof(summary));
  request.body = span_from_bytes(body, sizeof(body));
  request.has_image_path = 1U;
  request.image_path = span_from_bytes(path, sizeof(path));
  request.has_action = 1U;
  request.action_id = span_from_bytes(action_id, sizeof(action_id));
  request.action_label = span_from_bytes(action_label, sizeof(action_label));

  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_HINT,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_OK);
  assert_span_string(args.items[8], "string:image-path:file:///x%00%FF");
  assert_span_bytes(args.items[9], expected_action, sizeof(expected_action));
  assert_span_bytes(args.items[10], summary, sizeof(summary));
  assert_span_bytes(args.items[11], body, sizeof(body));
  g_assert_true(args.items[10].data == summary);
  g_assert_true(args.items[11].data == body);

  shaula_notify_send_args_clear(&args);
}

static void test_invalid_requests_and_output_reset(void) {
  ShaulaNotifyRequest request;
  ShaulaNotifySendArgs args = {0};

  shaula_notify_request_init(&request);
  request.summary = span_from_string("first");
  request.body = span_from_string("body");
  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_HINT,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_OK);
  g_assert_nonnull(args.timeout.data);

  request.summary = (ShaulaNotifySpan){NULL, 1U};
  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_HINT,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT);
  g_assert_cmpuint(args.length, ==, 0U);
  g_assert_null(args.timeout.data);

  shaula_notify_request_init(&request);
  request.has_image_path = 2U;
  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_HINT,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT);
  shaula_notify_request_init(&request);
  request.transient = 2U;
  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_HINT,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT);
  shaula_notify_request_init(&request);
  request.has_action = 2U;
  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_HINT,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT);

  shaula_notify_request_init(&request);
  request.urgency = SHAULA_NOTIFY_URGENCY_INVALID;
  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_HINT,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_INVALID_URGENCY);
  request.urgency = 3;
  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_HINT,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_INVALID_URGENCY);

  shaula_notify_request_init(&request);
  g_assert_cmpint(
      shaula_notify_send_args_build(&request,
                                    SHAULA_NOTIFY_IMAGE_MODE_INVALID, &args),
      ==, SHAULA_NOTIFY_STATUS_INVALID_IMAGE_MODE);
  g_assert_cmpint(shaula_notify_send_args_build(&request, 2, &args), ==,
                  SHAULA_NOTIFY_STATUS_INVALID_IMAGE_MODE);

  request.image_path = (ShaulaNotifySpan){NULL, 1U};
  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_HINT,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT);
  shaula_notify_request_init(&request);
  request.action_id = (ShaulaNotifySpan){NULL, 1U};
  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_HINT,
                                    &args),
      ==, SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT);

  g_assert_cmpint(
      shaula_notify_send_args_build(NULL, SHAULA_NOTIFY_IMAGE_MODE_HINT, &args),
      ==, SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(
      shaula_notify_send_args_build(&request, SHAULA_NOTIFY_IMAGE_MODE_HINT,
                                    NULL),
      ==, SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT);
  shaula_notify_send_args_clear(&args);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/notify/request/defaults-urgency",
                  test_request_defaults_and_urgency_tokens);
  g_test_add_func("/notify/request/default-argv",
                  test_default_argv_order_and_ownership);
  g_test_add_func("/notify/request/hint-action-max-timeout",
                  test_hint_argv_with_action_and_max_timeout);
  g_test_add_func("/notify/request/icon-nontransient",
                  test_icon_fallback_and_nontransient_argv);
  g_test_add_func("/notify/request/present-empty-optionals",
                  test_present_empty_optionals);
  g_test_add_func("/notify/request/uri-escaping", test_uri_byte_escaping);
  g_test_add_func("/notify/request/embedded-nul",
                  test_embedded_nul_and_arbitrary_bytes);
  g_test_add_func("/notify/request/invalid-reset",
                  test_invalid_requests_and_output_reset);
  return g_test_run();
}
