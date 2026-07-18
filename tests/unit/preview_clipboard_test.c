#include "preview_clipboard.h"

#include "clipboard/clipboard.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

static char *write_provider(const char *directory) {
  g_autofree char *path = g_build_filename(directory, "provider", NULL);
  const char *script =
      "#!/bin/sh\n"
      "cat > \"${SHAULA_TEST_PROVIDER_CAPTURE}\"\n"
      "case \"${SHAULA_TEST_PROVIDER_MODE:-ready}\" in\n"
      "  ready) printf 'READY shaula-clipboard/1\\n'; sleep 1 ;;\n"
      "  invalid) printf 'INVALID\\n' ;;\n"
      "  timeout) sleep 1 ;;\n"
      "  failure) exit 47 ;;\n"
      "esac\n";
  g_assert_true(g_file_set_contents(path, script, -1, NULL));
  g_assert_cmpint(g_chmod(path, 0755), ==, 0);
  return g_steal_pointer(&path);
}

static void assert_error(const GError *error, int code,
                         const char *message_fragment) {
  g_assert_nonnull(error);
  g_assert_cmpstr(g_quark_to_string(error->domain), ==,
                  "shaula-preview-clipboard");
  g_assert_cmpint(error->code, ==, code);
  g_assert_nonnull(strstr(error->message, message_fragment));
}

static void configure_provider(const char *provider, const char *capture,
                               const char *mode) {
  g_setenv("SHAULA_CLIPBOARD_PROVIDER_BIN", provider, TRUE);
  g_setenv("SHAULA_TEST_PROVIDER_CAPTURE", capture, TRUE);
  g_setenv("SHAULA_TEST_PROVIDER_MODE", mode, TRUE);
  g_unsetenv("SHAULA_CLIPBOARD_AVAILABLE");
  g_unsetenv("SHAULA_CLIPBOARD_READY_TIMEOUT_MS");
}

static void clear_provider_environment(void) {
  g_unsetenv("SHAULA_CLIPBOARD_PROVIDER_BIN");
  g_unsetenv("SHAULA_TEST_PROVIDER_CAPTURE");
  g_unsetenv("SHAULA_TEST_PROVIDER_MODE");
  g_unsetenv("SHAULA_CLIPBOARD_AVAILABLE");
  g_unsetenv("SHAULA_CLIPBOARD_READY_TIMEOUT_MS");
}

static void test_text_protocol(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *directory =
      g_dir_make_tmp("shaula-clipboard-text-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *provider = write_provider(directory);
  g_autofree char *capture = g_build_filename(directory, "capture", NULL);
  configure_provider(provider, capture, "ready");

  const char *text = "Shaula text ✓";
  g_assert_true(shaula_clipboard_copy_text(text, &error));
  g_assert_no_error(error);

  g_autofree char *contents = NULL;
  gsize length = 0U;
  g_assert_true(g_file_get_contents(capture, &contents, &length, &error));
  g_assert_no_error(error);
  g_autofree char *header = g_strdup_printf(
      "SHAULA-CLIPBOARD/1\nmime:text/plain;charset=utf-8\nlength:%zu\n\n",
      strlen(text));
  g_assert_cmpuint(length, ==, strlen(header) + strlen(text));
  g_assert_cmpmem(contents, strlen(header), header, strlen(header));
  g_assert_cmpmem(contents + strlen(header), strlen(text), text, strlen(text));

  clear_provider_environment();
  g_assert_cmpint(g_remove(capture), ==, 0);
  g_assert_cmpint(g_remove(provider), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
}

static void test_png_protocol(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *directory =
      g_dir_make_tmp("shaula-clipboard-png-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *provider = write_provider(directory);
  g_autofree char *capture = g_build_filename(directory, "capture", NULL);
  g_autofree char *png = g_build_filename(directory, "input.png", NULL);
  const guint8 payload[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0x00,
                            0xff, 0x42};
  g_assert_true(g_file_set_contents(png, (const char *)payload,
                                    (gssize)sizeof(payload), &error));
  g_assert_no_error(error);
  configure_provider(provider, capture, "ready");

  g_assert_true(shaula_clipboard_copy_png_file(png, &error));
  g_assert_no_error(error);

  g_autofree char *contents = NULL;
  gsize length = 0U;
  g_assert_true(g_file_get_contents(capture, &contents, &length, &error));
  g_assert_no_error(error);
  g_autofree char *header = g_strdup_printf(
      "SHAULA-CLIPBOARD/1\nmime:image/png\nlength:%zu\n\n", sizeof(payload));
  g_assert_cmpuint(length, ==, strlen(header) + sizeof(payload));
  g_assert_cmpmem(contents, strlen(header), header, strlen(header));
  g_assert_cmpmem(contents + strlen(header), sizeof(payload), payload,
                  sizeof(payload));

  clear_provider_environment();
  g_assert_cmpint(g_remove(capture), ==, 0);
  g_assert_cmpint(g_remove(png), ==, 0);
  g_assert_cmpint(g_remove(provider), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
}

static void test_missing_png(void) {
  g_autoptr(GError) error = NULL;
  g_assert_false(shaula_clipboard_copy_png_file(NULL, &error));
  assert_error(error, 1, "missing PNG path");
  g_clear_error(&error);
  g_assert_false(shaula_clipboard_copy_png_file("", &error));
  assert_error(error, 1, "missing PNG path");
}

static void test_provider_unavailable(void) {
  g_setenv("SHAULA_CLIPBOARD_AVAILABLE", "0", TRUE);
  g_autoptr(GError) error = NULL;
  g_assert_false(shaula_clipboard_copy_text("payload", &error));
  assert_error(error, 3, "unavailable");
  clear_provider_environment();
}

static void test_provider_protocol_invalid(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *directory =
      g_dir_make_tmp("shaula-clipboard-invalid-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *provider = write_provider(directory);
  g_autofree char *capture = g_build_filename(directory, "capture", NULL);
  configure_provider(provider, capture, "invalid");

  g_assert_false(shaula_clipboard_copy_text("payload", &error));
  assert_error(error, 3, "protocol_invalid");

  clear_provider_environment();
  g_assert_cmpint(g_remove(capture), ==, 0);
  g_assert_cmpint(g_remove(provider), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
}

static void test_provider_timeout(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *directory =
      g_dir_make_tmp("shaula-clipboard-timeout-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *provider = write_provider(directory);
  g_autofree char *capture = g_build_filename(directory, "capture", NULL);
  configure_provider(provider, capture, "timeout");
  g_setenv("SHAULA_CLIPBOARD_READY_TIMEOUT_MS", "25", TRUE);

  g_assert_false(shaula_clipboard_copy_text("payload", &error));
  assert_error(error, 3, "timeout");

  clear_provider_environment();
  if (g_file_test(capture, G_FILE_TEST_EXISTS))
    g_assert_cmpint(g_remove(capture), ==, 0);
  g_assert_cmpint(g_remove(provider), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
}

static void test_provider_spawn_failure(void) {
  g_setenv("SHAULA_CLIPBOARD_PROVIDER_BIN", "/nonexistent/shaula-provider",
           TRUE);
  g_autoptr(GError) error = NULL;
  g_assert_false(shaula_clipboard_copy_text("payload", &error));
  assert_error(error, 3, "unavailable");
  clear_provider_environment();
}

static void test_text_failure_message(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *directory =
      g_dir_make_tmp("shaula-clipboard-text-error-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *provider = write_provider(directory);
  g_autofree char *capture = g_build_filename(directory, "capture", NULL);
  configure_provider(provider, capture, "failure");

  g_assert_false(shaula_clipboard_copy_text("payload", &error));
  assert_error(error, 3, "Shaula clipboard text copy failed");
  g_assert_null(strstr(error->message, "image"));
  g_assert_nonnull(strstr(error->message, "provider_failed"));

  clear_provider_environment();
  g_assert_cmpint(g_remove(capture), ==, 0);
  g_assert_cmpint(g_remove(provider), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
}

static void test_provider_availability(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *directory =
      g_dir_make_tmp("shaula-clipboard-available-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *provider = write_provider(directory);
  g_autofree char *capture = g_build_filename(directory, "capture", NULL);
  configure_provider(provider, capture, "ready");
  g_assert_cmpint(shaula_clipboard_provider_available(), ==, 1);
  g_setenv("SHAULA_CLIPBOARD_AVAILABLE", "0", TRUE);
  g_assert_cmpint(shaula_clipboard_provider_available(), ==, 0);

  clear_provider_environment();
  g_assert_cmpint(g_remove(provider), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/preview/clipboard/text-protocol", test_text_protocol);
  g_test_add_func("/preview/clipboard/png-protocol", test_png_protocol);
  g_test_add_func("/preview/clipboard/png-missing", test_missing_png);
  g_test_add_func("/preview/clipboard/provider-unavailable",
                  test_provider_unavailable);
  g_test_add_func("/preview/clipboard/provider-protocol-invalid",
                  test_provider_protocol_invalid);
  g_test_add_func("/preview/clipboard/provider-timeout", test_provider_timeout);
  g_test_add_func("/preview/clipboard/provider-spawn-failure",
                  test_provider_spawn_failure);
  g_test_add_func("/preview/clipboard/text-failure-message",
                  test_text_failure_message);
  g_test_add_func("/preview/clipboard/provider-availability",
                  test_provider_availability);
  return g_test_run();
}
