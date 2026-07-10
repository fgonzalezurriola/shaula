#include <string.h>
#include <sys/wait.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "preview_clipboard.h"

static const char fake_wl_copy_script[] =
    "#!/bin/sh\n"
    "printf '%s\\n' \"$#\" > \"$SHAULA_TEST_ARGC\"\n"
    ": > \"$SHAULA_TEST_ARGV\"\n"
    "for arg in \"$@\"; do printf '%s\\000' \"$arg\" >> "
    "\"$SHAULA_TEST_ARGV\"; done\n"
    "/usr/bin/cat > \"$SHAULA_TEST_STDIN\"\n"
    "if [ -n \"$SHAULA_TEST_CHILD_STDOUT\" ]; then "
    "printf '%s' \"$SHAULA_TEST_CHILD_STDOUT\"; fi\n"
    "if [ -n \"$SHAULA_TEST_CHILD_STDERR\" ]; then "
    "printf '%s' \"$SHAULA_TEST_CHILD_STDERR\" >&2; fi\n"
    "exit \"${SHAULA_TEST_EXIT_CODE:-0}\"\n";

static const char fake_shaula_script[] =
    "#!/bin/sh\n"
    "printf '%s' \"$0\" > \"$SHAULA_TEST_SELECTED\"\n"
    "printf '%s\\n' \"$#\" > \"$SHAULA_TEST_ARGC\"\n"
    ": > \"$SHAULA_TEST_ARGV\"\n"
    "for arg in \"$@\"; do printf '%s\\000' \"$arg\" >> "
    "\"$SHAULA_TEST_ARGV\"; done\n"
    "if [ -n \"$SHAULA_TEST_CHILD_STDOUT\" ]; then "
    "printf '%s' \"$SHAULA_TEST_CHILD_STDOUT\"; fi\n"
    "if [ -n \"$SHAULA_TEST_CHILD_STDERR\" ]; then "
    "printf '%s' \"$SHAULA_TEST_CHILD_STDERR\" >&2; fi\n"
    "exit \"${SHAULA_TEST_EXIT_CODE:-0}\"\n";

static void restore_environment(const char *name, const char *value) {
  if (value != NULL)
    g_setenv(name, value, TRUE);
  else
    g_unsetenv(name);
}

static void remove_tree(const char *path) {
  if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
    g_remove(path);
    return;
  }

  GDir *directory = g_dir_open(path, 0, NULL);
  if (directory != NULL) {
    const char *name = NULL;
    while ((name = g_dir_read_name(directory)) != NULL) {
      g_autofree char *child = g_build_filename(path, name, NULL);
      remove_tree(child);
    }
    g_dir_close(directory);
  }
  g_rmdir(path);
}

static void write_executable(const char *path, const char *contents) {
  GError *error = NULL;
  g_assert_true(g_file_set_contents(path, contents, -1, &error));
  g_assert_no_error(error);
  g_assert_cmpint(g_chmod(path, 0700), ==, 0);
}

static char *copy_self_to_directory(const char *directory) {
  GError *error = NULL;
  g_autofree char *self = g_file_read_link("/proc/self/exe", &error);
  g_assert_no_error(error);
  g_assert_nonnull(self);

  g_autofree char *contents = NULL;
  gsize length = 0;
  g_assert_true(g_file_get_contents(self, &contents, &length, &error));
  g_assert_no_error(error);

  char *target = g_build_filename(directory, "clipboard-probe", NULL);
  g_assert_true(g_file_set_contents(target, contents, (gssize)length, &error));
  g_assert_no_error(error);
  g_assert_cmpint(g_chmod(target, 0700), ==, 0);
  return target;
}

static void assert_file_bytes(const char *path, const void *expected,
                              gsize expected_length) {
  GError *error = NULL;
  g_autofree char *actual = NULL;
  gsize actual_length = 0;
  g_assert_true(g_file_get_contents(path, &actual, &actual_length, &error));
  g_assert_no_error(error);
  g_assert_cmpuint(actual_length, ==, expected_length);
  g_assert_cmpmem(actual, actual_length, expected, expected_length);
}

static void assert_clipboard_error(const GError *error, int code,
                                   const char *message) {
  g_assert_nonnull(error);
  g_assert_cmpstr(g_quark_to_string(error->domain), ==,
                  "shaula-preview-clipboard");
  g_assert_cmpint(error->code, ==, code);
  g_assert_cmpstr(error->message, ==, message);
}

static int run_png_probe(const char *path) {
  const char *result_path = g_getenv("SHAULA_TEST_RESULT");
  if (result_path == NULL || result_path[0] == '\0')
    return 2;

  GError *error = NULL;
  gboolean ok = shaula_clipboard_copy_png_file(path, &error);

  g_autoptr(GKeyFile) result = g_key_file_new();
  g_key_file_set_boolean(result, "result", "ok", ok);
  if (error != NULL) {
    g_key_file_set_string(result, "result", "domain",
                          g_quark_to_string(error->domain));
    g_key_file_set_integer(result, "result", "code", error->code);
    g_key_file_set_string(result, "result", "message", error->message);
  }

  gsize data_length = 0;
  g_autofree char *data = g_key_file_to_data(result, &data_length, NULL);
  gboolean wrote =
      g_file_set_contents(result_path, data, (gssize)data_length, NULL);
  g_clear_error(&error);
  if (!wrote)
    return 3;
  return ok ? 0 : 1;
}

static GKeyFile *load_probe_result(const char *path) {
  GError *error = NULL;
  GKeyFile *result = g_key_file_new();
  g_assert_true(
      g_key_file_load_from_file(result, path, G_KEY_FILE_NONE, &error));
  g_assert_no_error(error);
  return result;
}

static void spawn_png_probe(const char *probe, const char *png_path,
                            char **stdout_text, char **stderr_text,
                            int *wait_status) {
  GError *error = NULL;
  char *argv[] = {(char *)probe, "--probe-png", (char *)png_path, NULL};
  g_assert_true(g_spawn_sync(NULL, argv, NULL, 0, NULL, NULL, stdout_text,
                             stderr_text, wait_status, &error));
  g_assert_no_error(error);
}

static void assert_text_case(const char *input, const char *expected,
                             const char *stdin_path, const char *argv_path,
                             const char *argc_path) {
  GError *error = NULL;
  g_assert_true(shaula_clipboard_copy_text(input, &error));
  g_assert_no_error(error);

  assert_file_bytes(stdin_path, expected, strlen(expected));
  static const char expected_argv[] = "--type\0text/plain\0";
  assert_file_bytes(argv_path, expected_argv, sizeof(expected_argv) - 1);
  assert_file_bytes(argc_path, "2\n", 2);
}

static void test_text_exact_stdin_and_argv(void) {
  GError *error = NULL;
  g_autofree char *tmp_dir = g_dir_make_tmp("shaula-clipboard-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(tmp_dir);

  g_autofree char *wl_copy = g_build_filename(tmp_dir, "wl-copy", NULL);
  g_autofree char *stdin_path = g_build_filename(tmp_dir, "stdin", NULL);
  g_autofree char *argv_path = g_build_filename(tmp_dir, "argv", NULL);
  g_autofree char *argc_path = g_build_filename(tmp_dir, "argc", NULL);
  g_autofree char *injected_path = g_build_filename(tmp_dir, "injected", NULL);
  write_executable(wl_copy, fake_wl_copy_script);

  g_autofree char *old_path = g_strdup(g_getenv("PATH"));
  g_autofree char *old_stdin = g_strdup(g_getenv("SHAULA_TEST_STDIN"));
  g_autofree char *old_argv = g_strdup(g_getenv("SHAULA_TEST_ARGV"));
  g_autofree char *old_argc = g_strdup(g_getenv("SHAULA_TEST_ARGC"));
  g_autofree char *old_exit = g_strdup(g_getenv("SHAULA_TEST_EXIT_CODE"));
  g_autofree char *old_stdout = g_strdup(g_getenv("SHAULA_TEST_CHILD_STDOUT"));
  g_autofree char *old_stderr = g_strdup(g_getenv("SHAULA_TEST_CHILD_STDERR"));

  g_setenv("PATH", tmp_dir, TRUE);
  g_setenv("SHAULA_TEST_STDIN", stdin_path, TRUE);
  g_setenv("SHAULA_TEST_ARGV", argv_path, TRUE);
  g_setenv("SHAULA_TEST_ARGC", argc_path, TRUE);
  g_setenv("SHAULA_TEST_EXIT_CODE", "0", TRUE);
  g_unsetenv("SHAULA_TEST_CHILD_STDOUT");
  g_unsetenv("SHAULA_TEST_CHILD_STDERR");

  assert_text_case(NULL, "", stdin_path, argv_path, argc_path);
  assert_text_case("", "", stdin_path, argv_path, argc_path);
  assert_text_case("text with spaces", "text with spaces", stdin_path,
                   argv_path, argc_path);
  assert_text_case("line one\nline two\n", "line one\nline two\n", stdin_path,
                   argv_path, argc_path);
  assert_text_case("'single' and \"double\" quotes",
                   "'single' and \"double\" quotes", stdin_path, argv_path,
                   argc_path);
  assert_text_case("100% literal %s and %%", "100% literal %s and %%",
                   stdin_path, argv_path, argc_path);
  assert_text_case("\n leading\tand trailing \n", "\n leading\tand trailing \n",
                   stdin_path, argv_path, argc_path);

  g_autofree char *metacharacters =
      g_strdup_printf("$(touch %s); `touch %s` | & < > * ? [abc]",
                      injected_path, injected_path);
  assert_text_case(metacharacters, metacharacters, stdin_path, argv_path,
                   argc_path);
  g_assert_false(g_file_test(injected_path, G_FILE_TEST_EXISTS));

  restore_environment("PATH", old_path);
  restore_environment("SHAULA_TEST_STDIN", old_stdin);
  restore_environment("SHAULA_TEST_ARGV", old_argv);
  restore_environment("SHAULA_TEST_ARGC", old_argc);
  restore_environment("SHAULA_TEST_EXIT_CODE", old_exit);
  restore_environment("SHAULA_TEST_CHILD_STDOUT", old_stdout);
  restore_environment("SHAULA_TEST_CHILD_STDERR", old_stderr);
  remove_tree(tmp_dir);
}

static void test_text_nonzero_exit(void) {
  GError *error = NULL;
  g_autofree char *tmp_dir = g_dir_make_tmp("shaula-clipboard-XXXXXX", &error);
  g_assert_no_error(error);

  g_autofree char *wl_copy = g_build_filename(tmp_dir, "wl-copy", NULL);
  g_autofree char *stdin_path = g_build_filename(tmp_dir, "stdin", NULL);
  g_autofree char *argv_path = g_build_filename(tmp_dir, "argv", NULL);
  g_autofree char *argc_path = g_build_filename(tmp_dir, "argc", NULL);
  write_executable(wl_copy, fake_wl_copy_script);

  g_autofree char *old_path = g_strdup(g_getenv("PATH"));
  g_autofree char *old_stdin = g_strdup(g_getenv("SHAULA_TEST_STDIN"));
  g_autofree char *old_argv = g_strdup(g_getenv("SHAULA_TEST_ARGV"));
  g_autofree char *old_argc = g_strdup(g_getenv("SHAULA_TEST_ARGC"));
  g_autofree char *old_exit = g_strdup(g_getenv("SHAULA_TEST_EXIT_CODE"));
  g_setenv("PATH", tmp_dir, TRUE);
  g_setenv("SHAULA_TEST_STDIN", stdin_path, TRUE);
  g_setenv("SHAULA_TEST_ARGV", argv_path, TRUE);
  g_setenv("SHAULA_TEST_ARGC", argc_path, TRUE);
  g_setenv("SHAULA_TEST_EXIT_CODE", "19", TRUE);

  g_assert_false(shaula_clipboard_copy_text("payload", &error));
  assert_clipboard_error(error, 3, "wl-copy text failed");
  g_clear_error(&error);
  assert_file_bytes(stdin_path, "payload", 7);

  restore_environment("PATH", old_path);
  restore_environment("SHAULA_TEST_STDIN", old_stdin);
  restore_environment("SHAULA_TEST_ARGV", old_argv);
  restore_environment("SHAULA_TEST_ARGC", old_argc);
  restore_environment("SHAULA_TEST_EXIT_CODE", old_exit);
  remove_tree(tmp_dir);
}

static void test_text_spawn_failure(void) {
  GError *error = NULL;
  g_autofree char *tmp_dir = g_dir_make_tmp("shaula-clipboard-XXXXXX", &error);
  g_assert_no_error(error);

  g_autofree char *old_path = g_strdup(g_getenv("PATH"));
  g_setenv("PATH", tmp_dir, TRUE);
  g_assert_false(shaula_clipboard_copy_text("payload", &error));
  restore_environment("PATH", old_path);

  g_assert_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT);
  g_assert_nonnull(strstr(error->message, "wl-copy"));
  g_clear_error(&error);
  remove_tree(tmp_dir);
}

static void test_text_stdio_contract(void) {
  if (g_test_subprocess()) {
    GError *error = NULL;
    g_autofree char *tmp_dir =
        g_dir_make_tmp("shaula-clipboard-XXXXXX", &error);
    g_assert_no_error(error);

    g_autofree char *wl_copy = g_build_filename(tmp_dir, "wl-copy", NULL);
    g_autofree char *stdin_path = g_build_filename(tmp_dir, "stdin", NULL);
    g_autofree char *argv_path = g_build_filename(tmp_dir, "argv", NULL);
    g_autofree char *argc_path = g_build_filename(tmp_dir, "argc", NULL);
    write_executable(wl_copy, fake_wl_copy_script);
    g_setenv("PATH", tmp_dir, TRUE);
    g_setenv("SHAULA_TEST_STDIN", stdin_path, TRUE);
    g_setenv("SHAULA_TEST_ARGV", argv_path, TRUE);
    g_setenv("SHAULA_TEST_ARGC", argc_path, TRUE);
    g_setenv("SHAULA_TEST_EXIT_CODE", "0", TRUE);
    g_setenv("SHAULA_TEST_CHILD_STDOUT", "wl-copy-stdout", TRUE);
    g_setenv("SHAULA_TEST_CHILD_STDERR", "wl-copy-stderr", TRUE);

    g_assert_true(shaula_clipboard_copy_text("payload", &error));
    g_assert_no_error(error);
    remove_tree(tmp_dir);
    return;
  }

  g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
  g_test_trap_assert_passed();
  g_test_trap_assert_stdout("");
  g_test_trap_assert_stderr("wl-copy-stderr");
}

static void test_png_missing_path(void) {
  GError *error = NULL;
  g_assert_false(shaula_clipboard_copy_png_file(NULL, &error));
  assert_clipboard_error(error, 1, "missing PNG path");
  g_clear_error(&error);

  g_assert_false(shaula_clipboard_copy_png_file("", &error));
  assert_clipboard_error(error, 1, "missing PNG path");
  g_clear_error(&error);
}

static void test_png_prefers_sibling_and_uses_exact_argv(void) {
  GError *error = NULL;
  g_autofree char *root = g_dir_make_tmp("shaula-clipboard-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *probe_dir = g_build_filename(root, "probe", NULL);
  g_autofree char *path_dir = g_build_filename(root, "path", NULL);
  g_assert_cmpint(g_mkdir(probe_dir, 0700), ==, 0);
  g_assert_cmpint(g_mkdir(path_dir, 0700), ==, 0);

  g_autofree char *probe = copy_self_to_directory(probe_dir);
  g_autofree char *sibling = g_build_filename(probe_dir, "shaula", NULL);
  g_autofree char *path_shaula = g_build_filename(path_dir, "shaula", NULL);
  g_autofree char *selected = g_build_filename(root, "selected", NULL);
  g_autofree char *argv_path = g_build_filename(root, "argv", NULL);
  g_autofree char *argc_path = g_build_filename(root, "argc", NULL);
  g_autofree char *result_path = g_build_filename(root, "result.ini", NULL);
  g_autofree char *injected_path = g_build_filename(root, "injected", NULL);
  g_autofree char *png_path =
      g_strdup_printf("%s/capture $(touch %s); image.png", root, injected_path);
  write_executable(sibling, fake_shaula_script);
  write_executable(path_shaula, fake_shaula_script);

  g_autofree char *old_path = g_strdup(g_getenv("PATH"));
  g_autofree char *old_selected = g_strdup(g_getenv("SHAULA_TEST_SELECTED"));
  g_autofree char *old_argv = g_strdup(g_getenv("SHAULA_TEST_ARGV"));
  g_autofree char *old_argc = g_strdup(g_getenv("SHAULA_TEST_ARGC"));
  g_autofree char *old_result = g_strdup(g_getenv("SHAULA_TEST_RESULT"));
  g_autofree char *old_exit = g_strdup(g_getenv("SHAULA_TEST_EXIT_CODE"));
  g_autofree char *old_stdout = g_strdup(g_getenv("SHAULA_TEST_CHILD_STDOUT"));
  g_autofree char *old_stderr = g_strdup(g_getenv("SHAULA_TEST_CHILD_STDERR"));
  g_setenv("PATH", path_dir, TRUE);
  g_setenv("SHAULA_TEST_SELECTED", selected, TRUE);
  g_setenv("SHAULA_TEST_ARGV", argv_path, TRUE);
  g_setenv("SHAULA_TEST_ARGC", argc_path, TRUE);
  g_setenv("SHAULA_TEST_RESULT", result_path, TRUE);
  g_setenv("SHAULA_TEST_EXIT_CODE", "0", TRUE);
  g_setenv("SHAULA_TEST_CHILD_STDOUT", "shaula-stdout", TRUE);
  g_setenv("SHAULA_TEST_CHILD_STDERR", "shaula-stderr", TRUE);

  g_autofree char *stdout_text = NULL;
  g_autofree char *stderr_text = NULL;
  int wait_status = 0;
  spawn_png_probe(probe, png_path, &stdout_text, &stderr_text, &wait_status);

  restore_environment("PATH", old_path);
  restore_environment("SHAULA_TEST_SELECTED", old_selected);
  restore_environment("SHAULA_TEST_ARGV", old_argv);
  restore_environment("SHAULA_TEST_ARGC", old_argc);
  restore_environment("SHAULA_TEST_RESULT", old_result);
  restore_environment("SHAULA_TEST_EXIT_CODE", old_exit);
  restore_environment("SHAULA_TEST_CHILD_STDOUT", old_stdout);
  restore_environment("SHAULA_TEST_CHILD_STDERR", old_stderr);

  g_assert_true(WIFEXITED(wait_status));
  g_assert_cmpint(WEXITSTATUS(wait_status), ==, 0);
  g_assert_cmpstr(stdout_text, ==, "");
  g_assert_cmpstr(stderr_text, ==, "");
  assert_file_bytes(selected, sibling, strlen(sibling));
  assert_file_bytes(argc_path, "5\n", 2);

  g_autoptr(GByteArray) expected_argv = g_byte_array_new();
  const char *expected_arguments[] = {"clipboard", "copy-image", "--input",
                                      png_path,    "--json",     NULL};
  for (gsize index = 0; expected_arguments[index] != NULL; index += 1) {
    gsize argument_length = strlen(expected_arguments[index]) + 1;
    g_assert_true(argument_length <= G_MAXUINT);
    g_byte_array_append(expected_argv,
                        (const guint8 *)expected_arguments[index],
                        (guint)argument_length);
  }
  assert_file_bytes(argv_path, expected_argv->data, expected_argv->len);
  g_assert_false(g_file_test(injected_path, G_FILE_TEST_EXISTS));

  g_autoptr(GKeyFile) result = load_probe_result(result_path);
  g_assert_true(g_key_file_get_boolean(result, "result", "ok", &error));
  g_assert_no_error(error);
  remove_tree(root);
}

static void test_png_path_fallback(void) {
  GError *error = NULL;
  g_autofree char *root = g_dir_make_tmp("shaula-clipboard-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *probe_dir = g_build_filename(root, "probe", NULL);
  g_autofree char *path_dir = g_build_filename(root, "path", NULL);
  g_assert_cmpint(g_mkdir(probe_dir, 0700), ==, 0);
  g_assert_cmpint(g_mkdir(path_dir, 0700), ==, 0);

  g_autofree char *probe = copy_self_to_directory(probe_dir);
  g_autofree char *path_shaula = g_build_filename(path_dir, "shaula", NULL);
  g_autofree char *selected = g_build_filename(root, "selected", NULL);
  g_autofree char *argv_path = g_build_filename(root, "argv", NULL);
  g_autofree char *argc_path = g_build_filename(root, "argc", NULL);
  g_autofree char *result_path = g_build_filename(root, "result.ini", NULL);
  write_executable(path_shaula, fake_shaula_script);

  g_autofree char *old_path = g_strdup(g_getenv("PATH"));
  g_autofree char *old_selected = g_strdup(g_getenv("SHAULA_TEST_SELECTED"));
  g_autofree char *old_argv = g_strdup(g_getenv("SHAULA_TEST_ARGV"));
  g_autofree char *old_argc = g_strdup(g_getenv("SHAULA_TEST_ARGC"));
  g_autofree char *old_result = g_strdup(g_getenv("SHAULA_TEST_RESULT"));
  g_autofree char *old_exit = g_strdup(g_getenv("SHAULA_TEST_EXIT_CODE"));
  g_autofree char *old_stdout = g_strdup(g_getenv("SHAULA_TEST_CHILD_STDOUT"));
  g_autofree char *old_stderr = g_strdup(g_getenv("SHAULA_TEST_CHILD_STDERR"));
  g_setenv("PATH", path_dir, TRUE);
  g_setenv("SHAULA_TEST_SELECTED", selected, TRUE);
  g_setenv("SHAULA_TEST_ARGV", argv_path, TRUE);
  g_setenv("SHAULA_TEST_ARGC", argc_path, TRUE);
  g_setenv("SHAULA_TEST_RESULT", result_path, TRUE);
  g_setenv("SHAULA_TEST_EXIT_CODE", "0", TRUE);
  g_unsetenv("SHAULA_TEST_CHILD_STDOUT");
  g_unsetenv("SHAULA_TEST_CHILD_STDERR");

  g_autofree char *stdout_text = NULL;
  g_autofree char *stderr_text = NULL;
  int wait_status = 0;
  spawn_png_probe(probe, "/tmp/capture.png", &stdout_text, &stderr_text,
                  &wait_status);

  restore_environment("PATH", old_path);
  restore_environment("SHAULA_TEST_SELECTED", old_selected);
  restore_environment("SHAULA_TEST_ARGV", old_argv);
  restore_environment("SHAULA_TEST_ARGC", old_argc);
  restore_environment("SHAULA_TEST_RESULT", old_result);
  restore_environment("SHAULA_TEST_EXIT_CODE", old_exit);
  restore_environment("SHAULA_TEST_CHILD_STDOUT", old_stdout);
  restore_environment("SHAULA_TEST_CHILD_STDERR", old_stderr);

  g_assert_true(WIFEXITED(wait_status));
  g_assert_cmpint(WEXITSTATUS(wait_status), ==, 0);
  assert_file_bytes(selected, path_shaula, strlen(path_shaula));
  remove_tree(root);
}

static void test_png_nonzero_exit(void) {
  GError *error = NULL;
  g_autofree char *root = g_dir_make_tmp("shaula-clipboard-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *probe_dir = g_build_filename(root, "probe", NULL);
  g_assert_cmpint(g_mkdir(probe_dir, 0700), ==, 0);

  g_autofree char *probe = copy_self_to_directory(probe_dir);
  g_autofree char *sibling = g_build_filename(probe_dir, "shaula", NULL);
  g_autofree char *selected = g_build_filename(root, "selected", NULL);
  g_autofree char *argv_path = g_build_filename(root, "argv", NULL);
  g_autofree char *argc_path = g_build_filename(root, "argc", NULL);
  g_autofree char *result_path = g_build_filename(root, "result.ini", NULL);
  write_executable(sibling, fake_shaula_script);

  g_autofree char *old_selected = g_strdup(g_getenv("SHAULA_TEST_SELECTED"));
  g_autofree char *old_argv = g_strdup(g_getenv("SHAULA_TEST_ARGV"));
  g_autofree char *old_argc = g_strdup(g_getenv("SHAULA_TEST_ARGC"));
  g_autofree char *old_result = g_strdup(g_getenv("SHAULA_TEST_RESULT"));
  g_autofree char *old_exit = g_strdup(g_getenv("SHAULA_TEST_EXIT_CODE"));
  g_setenv("SHAULA_TEST_SELECTED", selected, TRUE);
  g_setenv("SHAULA_TEST_ARGV", argv_path, TRUE);
  g_setenv("SHAULA_TEST_ARGC", argc_path, TRUE);
  g_setenv("SHAULA_TEST_RESULT", result_path, TRUE);
  g_setenv("SHAULA_TEST_EXIT_CODE", "23", TRUE);

  g_autofree char *stdout_text = NULL;
  g_autofree char *stderr_text = NULL;
  int wait_status = 0;
  spawn_png_probe(probe, "/tmp/capture.png", &stdout_text, &stderr_text,
                  &wait_status);

  restore_environment("SHAULA_TEST_SELECTED", old_selected);
  restore_environment("SHAULA_TEST_ARGV", old_argv);
  restore_environment("SHAULA_TEST_ARGC", old_argc);
  restore_environment("SHAULA_TEST_RESULT", old_result);
  restore_environment("SHAULA_TEST_EXIT_CODE", old_exit);

  g_assert_true(WIFEXITED(wait_status));
  g_assert_cmpint(WEXITSTATUS(wait_status), ==, 1);
  g_autoptr(GKeyFile) result = load_probe_result(result_path);
  g_assert_false(g_key_file_get_boolean(result, "result", "ok", &error));
  g_assert_no_error(error);
  g_autofree char *domain =
      g_key_file_get_string(result, "result", "domain", &error);
  g_assert_no_error(error);
  g_assert_cmpstr(domain, ==, "shaula-preview-clipboard");
  g_assert_cmpint(g_key_file_get_integer(result, "result", "code", &error), ==,
                  2);
  g_assert_no_error(error);
  g_autofree char *message =
      g_key_file_get_string(result, "result", "message", &error);
  g_assert_no_error(error);
  g_assert_cmpstr(message, ==, "shaula clipboard copy-image failed");
  remove_tree(root);
}

static void test_png_spawn_failure(void) {
  GError *error = NULL;
  g_autofree char *root = g_dir_make_tmp("shaula-clipboard-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *probe_dir = g_build_filename(root, "probe", NULL);
  g_autofree char *empty_path = g_build_filename(root, "empty-path", NULL);
  g_assert_cmpint(g_mkdir(probe_dir, 0700), ==, 0);
  g_assert_cmpint(g_mkdir(empty_path, 0700), ==, 0);

  g_autofree char *probe = copy_self_to_directory(probe_dir);
  g_autofree char *result_path = g_build_filename(root, "result.ini", NULL);
  g_autofree char *old_path = g_strdup(g_getenv("PATH"));
  g_autofree char *old_result = g_strdup(g_getenv("SHAULA_TEST_RESULT"));
  g_setenv("PATH", empty_path, TRUE);
  g_setenv("SHAULA_TEST_RESULT", result_path, TRUE);

  g_autofree char *stdout_text = NULL;
  g_autofree char *stderr_text = NULL;
  int wait_status = 0;
  spawn_png_probe(probe, "/tmp/capture.png", &stdout_text, &stderr_text,
                  &wait_status);

  restore_environment("PATH", old_path);
  restore_environment("SHAULA_TEST_RESULT", old_result);

  g_assert_true(WIFEXITED(wait_status));
  g_assert_cmpint(WEXITSTATUS(wait_status), ==, 1);
  g_autoptr(GKeyFile) result = load_probe_result(result_path);
  g_autofree char *domain =
      g_key_file_get_string(result, "result", "domain", &error);
  g_assert_no_error(error);
  g_assert_cmpstr(domain, ==, g_quark_to_string(G_SPAWN_ERROR));
  g_assert_cmpint(g_key_file_get_integer(result, "result", "code", &error), ==,
                  G_SPAWN_ERROR_NOENT);
  g_assert_no_error(error);
  g_autofree char *message =
      g_key_file_get_string(result, "result", "message", &error);
  g_assert_no_error(error);
  g_assert_nonnull(strstr(message, "shaula"));
  remove_tree(root);
}

int main(int argc, char **argv) {
  if (argc == 3 && g_strcmp0(argv[1], "--probe-png") == 0)
    return run_png_probe(argv[2]);

  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/preview/clipboard/text-exact-stdin-argv",
                  test_text_exact_stdin_and_argv);
  g_test_add_func("/preview/clipboard/text-nonzero-exit",
                  test_text_nonzero_exit);
  g_test_add_func("/preview/clipboard/text-spawn-failure",
                  test_text_spawn_failure);
  g_test_add_func("/preview/clipboard/text-stdio", test_text_stdio_contract);
  g_test_add_func("/preview/clipboard/png-missing-path", test_png_missing_path);
  g_test_add_func("/preview/clipboard/png-sibling-exact-argv",
                  test_png_prefers_sibling_and_uses_exact_argv);
  g_test_add_func("/preview/clipboard/png-path-fallback",
                  test_png_path_fallback);
  g_test_add_func("/preview/clipboard/png-nonzero-exit", test_png_nonzero_exit);
  g_test_add_func("/preview/clipboard/png-spawn-failure",
                  test_png_spawn_failure);
  return g_test_run();
}
