#define _POSIX_C_SOURCE 200809L

#include "process_exec.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *self_path;

static int helper_write_all(int fd, const char *data, size_t length) {
  size_t written = 0;

  while (written < length) {
    ssize_t result = write(fd, data + written, length - written);
    if (result > 0) {
      written += (size_t)result;
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    return 0;
  }
  return 1;
}

static int helper_copy_stdin(const char *path, int exit_code) {
  char buffer[4096];
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);

  if (fd < 0) {
    return 120;
  }
  for (;;) {
    ssize_t result = read(STDIN_FILENO, buffer, sizeof(buffer));
    if (result > 0) {
      if (!helper_write_all(fd, buffer, (size_t)result)) {
        close(fd);
        return 121;
      }
      continue;
    }
    if (result == 0) {
      break;
    }
    if (errno == EINTR) {
      continue;
    }
    close(fd);
    return 122;
  }

  close(fd);
  return exit_code;
}

static int helper_main(int argc, char **argv) {
  const char *mode;

  if (argc < 3 || strcmp(argv[1], "--helper") != 0) {
    return -1;
  }
  mode = argv[2];

  if (strcmp(mode, "emit") == 0) {
    static const char stdout_bytes[] = {'o', 'u', 't', '\0', 'x'};
    static const char stderr_bytes[] = "err\n";
    int exit_code = argc >= 4 ? atoi(argv[3]) : 0;

    if (!helper_write_all(STDOUT_FILENO, stdout_bytes,
                          sizeof(stdout_bytes)) ||
        !helper_write_all(STDERR_FILENO, stderr_bytes,
                          sizeof(stderr_bytes) - 1)) {
      return 123;
    }
    return exit_code;
  }

  if (strcmp(mode, "flood") == 0) {
    char stdout_chunk[1024];
    char stderr_chunk[1024];
    size_t remaining = argc >= 4 ? (size_t)g_ascii_strtoull(argv[3], NULL, 10)
                                 : 0;

    memset(stdout_chunk, 'O', sizeof(stdout_chunk));
    memset(stderr_chunk, 'E', sizeof(stderr_chunk));
    while (remaining > 0) {
      size_t amount = remaining < sizeof(stdout_chunk) ? remaining
                                                       : sizeof(stdout_chunk);
      if (!helper_write_all(STDOUT_FILENO, stdout_chunk, amount) ||
          !helper_write_all(STDERR_FILENO, stderr_chunk, amount)) {
        return 124;
      }
      remaining -= amount;
    }
    return 0;
  }

  if (strcmp(mode, "flood-stdout") == 0) {
    char chunk[1024];
    size_t remaining = argc >= 4 ? (size_t)g_ascii_strtoull(argv[3], NULL, 10)
                                 : 0;

    memset(chunk, 'X', sizeof(chunk));
    while (remaining > 0) {
      size_t amount = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
      if (!helper_write_all(STDOUT_FILENO, chunk, amount)) {
        return 125;
      }
      remaining -= amount;
    }
    return 0;
  }

  if (strcmp(mode, "env") == 0) {
    const char *only = getenv("ONLY");
    const char *path = getenv("PATH");
    g_autofree char *message =
        g_strdup_printf("%s|%s", only != NULL ? only : "missing",
                        path != NULL ? path : "missing");
    return helper_write_all(STDOUT_FILENO, message, strlen(message)) ? 0 : 126;
  }

  if (strcmp(mode, "echo-arg") == 0) {
    if (argc < 4) {
      return 127;
    }
    return helper_write_all(STDOUT_FILENO, argv[3], strlen(argv[3])) ? 0 : 128;
  }

  if (strcmp(mode, "copy-stdin") == 0) {
    if (argc < 5) {
      return 129;
    }
    return helper_copy_stdin(argv[3], atoi(argv[4]));
  }

  if (strcmp(mode, "close-stdin") == 0) {
    close(STDIN_FILENO);
    g_usleep(50000);
    return 0;
  }

  if (strcmp(mode, "signal") == 0) {
    raise(SIGTERM);
    return 130;
  }

  return 131;
}

static ShaulaProcessArgv argv_from_strings(const char *const *values,
                                           size_t length,
                                           ShaulaProcessSpan *spans) {
  size_t index;

  for (index = 0; index < length; index += 1) {
    spans[index] = (ShaulaProcessSpan){values[index], strlen(values[index])};
  }
  return (ShaulaProcessArgv){spans, length};
}

static void assert_all_bytes(const char *data, size_t length, char expected) {
  size_t index;

  for (index = 0; index < length; index += 1) {
    g_assert_cmpint((unsigned char)data[index], ==, (unsigned char)expected);
  }
}

static void test_binary_output_exit_and_literal_argv(void) {
  static const char expected_stdout[] = {'o', 'u', 't', '\0', 'x'};
  const char *emit_values[] = {self_path, "--helper", "emit", "7"};
  ShaulaProcessSpan emit_spans[G_N_ELEMENTS(emit_values)];
  ShaulaProcessOutput output;
  const char *literal = "$(touch /tmp/shaula-process-should-not-exist); * ' \"";
  const char *echo_values[] = {self_path, "--helper", "echo-arg", literal};
  ShaulaProcessSpan echo_spans[G_N_ELEMENTS(echo_values)];

  g_assert_cmpint(
      shaula_process_run(argv_from_strings(emit_values,
                                           G_N_ELEMENTS(emit_values),
                                           emit_spans),
                         NULL, sizeof(expected_stdout), 4, &output),
      ==, SHAULA_PROCESS_STATUS_OK);
  g_assert_cmpint(output.term_kind, ==, SHAULA_PROCESS_TERM_EXITED);
  g_assert_cmpuint(output.term_value, ==, 7);
  g_assert_cmpuint(output.stdout_bytes.length, ==, sizeof(expected_stdout));
  g_assert_cmpmem(output.stdout_bytes.data, output.stdout_bytes.length,
                  expected_stdout, sizeof(expected_stdout));
  g_assert_cmpuint(output.stderr_bytes.length, ==, 4);
  g_assert_cmpmem(output.stderr_bytes.data, output.stderr_bytes.length, "err\n",
                  4);
  shaula_process_output_clear(&output);
  shaula_process_output_clear(&output);

  g_assert_cmpint(
      shaula_process_run(argv_from_strings(echo_values,
                                           G_N_ELEMENTS(echo_values),
                                           echo_spans),
                         NULL, strlen(literal), 0, &output),
      ==, SHAULA_PROCESS_STATUS_OK);
  g_assert_cmpint(output.term_kind, ==, SHAULA_PROCESS_TERM_EXITED);
  g_assert_cmpuint(output.term_value, ==, 0);
  g_assert_cmpuint(output.stdout_bytes.length, ==, strlen(literal));
  g_assert_cmpmem(output.stdout_bytes.data, output.stdout_bytes.length, literal,
                  strlen(literal));
  g_assert_cmpuint(output.stderr_bytes.length, ==, 0);
  shaula_process_output_clear(&output);
}

static void test_dual_stream_drain_and_limits(void) {
  const size_t stream_size = 256 * 1024;
  g_autofree char *stream_size_text = g_strdup_printf("%zu", stream_size);
  const char *flood_values[] = {self_path, "--helper", "flood",
                                stream_size_text};
  ShaulaProcessSpan flood_spans[G_N_ELEMENTS(flood_values)];
  const char *limit_values[] = {self_path, "--helper", "flood-stdout", "65"};
  ShaulaProcessSpan limit_spans[G_N_ELEMENTS(limit_values)];
  ShaulaProcessOutput output;

  g_assert_cmpint(
      shaula_process_run(argv_from_strings(flood_values,
                                           G_N_ELEMENTS(flood_values),
                                           flood_spans),
                         NULL, stream_size, stream_size, &output),
      ==, SHAULA_PROCESS_STATUS_OK);
  g_assert_cmpuint(output.stdout_bytes.length, ==, stream_size);
  g_assert_cmpuint(output.stderr_bytes.length, ==, stream_size);
  assert_all_bytes(output.stdout_bytes.data, output.stdout_bytes.length, 'O');
  assert_all_bytes(output.stderr_bytes.data, output.stderr_bytes.length, 'E');
  g_assert_cmpint(output.term_kind, ==, SHAULA_PROCESS_TERM_EXITED);
  g_assert_cmpuint(output.term_value, ==, 0);
  shaula_process_output_clear(&output);

  output = (ShaulaProcessOutput){
      .stdout_bytes = {(char *)0x1, 99},
      .stderr_bytes = {(char *)0x1, 99},
      .term_kind = 99,
      .term_value = 99,
  };
  g_assert_cmpint(
      shaula_process_run(argv_from_strings(limit_values,
                                           G_N_ELEMENTS(limit_values),
                                           limit_spans),
                         NULL, 64, 0, &output),
      ==, SHAULA_PROCESS_STATUS_STREAM_TOO_LONG);
  g_assert_null(output.stdout_bytes.data);
  g_assert_cmpuint(output.stdout_bytes.length, ==, 0);
  g_assert_null(output.stderr_bytes.data);
  g_assert_cmpuint(output.stderr_bytes.length, ==, 0);
}

static void test_replacement_environment_uses_parent_path(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-process-path-XXXXXX", &error);
  g_autofree char *helper_path = NULL;
  g_autofree char *old_path = g_strdup(g_getenv("PATH"));
  g_autofree char *new_path = NULL;
  const char *helper_name = "shaula-process-test-helper";
  const char *values[] = {helper_name, "--helper", "env"};
  ShaulaProcessSpan spans[G_N_ELEMENTS(values)];
  const char *const replacement_environment[] = {"ONLY=custom", NULL};
  ShaulaProcessOutput output;

  g_assert_no_error(error);
  g_assert_nonnull(root);
  helper_path = g_build_filename(root, helper_name, NULL);
  g_assert_cmpint(symlink(self_path, helper_path), ==, 0);
  new_path = g_strdup_printf("%s:%s", root,
                             old_path != NULL ? old_path : "");
  g_assert_true(g_setenv("PATH", new_path, TRUE));

  g_assert_cmpint(
      shaula_process_run(argv_from_strings(values, G_N_ELEMENTS(values), spans),
                         replacement_environment, 64, 64, &output),
      ==, SHAULA_PROCESS_STATUS_OK);
  g_assert_cmpint(output.term_kind, ==, SHAULA_PROCESS_TERM_EXITED);
  g_assert_cmpuint(output.term_value, ==, 0);
  g_assert_cmpuint(output.stdout_bytes.length, ==,
                   strlen("custom|missing"));
  g_assert_cmpmem(output.stdout_bytes.data, output.stdout_bytes.length,
                  "custom|missing", strlen("custom|missing"));
  g_assert_cmpuint(output.stderr_bytes.length, ==, 0);
  shaula_process_output_clear(&output);

  if (old_path != NULL) {
    g_assert_true(g_setenv("PATH", old_path, TRUE));
  } else {
    g_unsetenv("PATH");
  }
  g_assert_cmpint(g_remove(helper_path), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_parent_path_and_exec_parity(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-process-path-parity-XXXXXX", &error);
  g_autofree char *old_cwd = g_get_current_dir();
  g_autofree char *old_path = g_strdup(g_getenv("PATH"));
  g_autofree char *helper_path = NULL;
  g_autofree char *text_path = NULL;
  g_autofree char *long_path = g_malloc0(PATH_MAX);
  const char *helper_name = "shaula-process-cwd-only-helper";
  const char *bare_values[] = {helper_name};
  ShaulaProcessSpan bare_spans[G_N_ELEMENTS(bare_values)];
  const char *text_values[1];
  ShaulaProcessSpan text_spans[1];
  const char *long_values[] = {"x"};
  ShaulaProcessSpan long_spans[G_N_ELEMENTS(long_values)];
  ShaulaProcessOutput output;

  g_assert_no_error(error);
  g_assert_nonnull(root);
  helper_path = g_build_filename(root, helper_name, NULL);
  text_path = g_build_filename(root, "plain-executable-text", NULL);
  g_assert_cmpint(symlink(self_path, helper_path), ==, 0);
  g_assert_true(g_file_set_contents(text_path, "exit 0\n", -1, &error));
  g_assert_no_error(error);
  g_assert_cmpint(g_chmod(text_path, 0755), ==, 0);
  g_assert_cmpint(g_chdir(root), ==, 0);

  g_assert_true(g_setenv("PATH", ":/definitely/missing:", TRUE));
  g_assert_cmpint(
      shaula_process_run(argv_from_strings(bare_values,
                                           G_N_ELEMENTS(bare_values),
                                           bare_spans),
                         NULL, 0, 0, &output),
      ==, SHAULA_PROCESS_STATUS_FILE_NOT_FOUND);

  text_values[0] = text_path;
  g_assert_cmpint(
      shaula_process_run(argv_from_strings(text_values, 1, text_spans), NULL, 0,
                         0, &output),
      ==, SHAULA_PROCESS_STATUS_INVALID_EXECUTABLE);

  memset(long_path, 'a', PATH_MAX - 1);
  long_path[PATH_MAX - 1] = '\0';
  g_assert_true(g_setenv("PATH", long_path, TRUE));
  g_assert_cmpint(
      shaula_process_run(argv_from_strings(long_values,
                                           G_N_ELEMENTS(long_values),
                                           long_spans),
                         NULL, 0, 0, &output),
      ==, SHAULA_PROCESS_STATUS_NAME_TOO_LONG);

  g_assert_cmpint(g_chdir(old_cwd), ==, 0);
  if (old_path != NULL) {
    g_assert_true(g_setenv("PATH", old_path, TRUE));
  } else {
    g_unsetenv("PATH");
  }
  g_assert_cmpint(g_remove(helper_path), ==, 0);
  g_assert_cmpint(g_remove(text_path), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_spawn_errors_and_invalid_arguments(void) {
  const char *missing_values[] = {
      "shaula-process-command-that-does-not-exist-7d2d6c"};
  ShaulaProcessSpan missing_spans[G_N_ELEMENTS(missing_values)];
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-process-errors-XXXXXX", &error);
  g_autofree char *non_executable = NULL;
  const char *non_exec_values[1];
  ShaulaProcessSpan non_exec_spans[1];
  static const char embedded_nul[] = {'a', '\0', 'b'};
  ShaulaProcessSpan invalid_item = {embedded_nul, sizeof(embedded_nul)};
  ShaulaProcessOutput output;

  g_assert_no_error(error);
  g_assert_nonnull(root);
  non_executable = g_build_filename(root, "not-executable", NULL);
  g_assert_true(g_file_set_contents(non_executable, "not an executable", -1,
                                    &error));
  g_assert_no_error(error);
  g_assert_cmpint(g_chmod(non_executable, 0644), ==, 0);
  non_exec_values[0] = non_executable;

  g_assert_cmpint(
      shaula_process_run(argv_from_strings(missing_values,
                                           G_N_ELEMENTS(missing_values),
                                           missing_spans),
                         NULL, 0, 0, &output),
      ==, SHAULA_PROCESS_STATUS_FILE_NOT_FOUND);
  g_assert_cmpint(
      shaula_process_run(argv_from_strings(non_exec_values, 1, non_exec_spans),
                         NULL, 0, 0, &output),
      ==, SHAULA_PROCESS_STATUS_ACCESS_DENIED);
  g_assert_cmpint(shaula_process_run((ShaulaProcessArgv){NULL, 0}, NULL, 0, 0,
                                     &output),
                  ==, SHAULA_PROCESS_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(shaula_process_run((ShaulaProcessArgv){NULL, 1}, NULL, 0, 0,
                                     &output),
                  ==, SHAULA_PROCESS_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(shaula_process_run((ShaulaProcessArgv){&invalid_item, 1}, NULL,
                                     0, 0, &output),
                  ==, SHAULA_PROCESS_STATUS_INVALID_ARGUMENT);
  invalid_item = (ShaulaProcessSpan){"x", SIZE_MAX};
  g_assert_cmpint(shaula_process_run((ShaulaProcessArgv){&invalid_item, 1}, NULL,
                                     0, 0, &output),
                  ==, SHAULA_PROCESS_STATUS_OUT_OF_MEMORY);
  g_assert_cmpint(shaula_process_run(
                      argv_from_strings(missing_values,
                                        G_N_ELEMENTS(missing_values),
                                        missing_spans),
                      NULL, 0, 0, NULL),
                  ==, SHAULA_PROCESS_STATUS_INVALID_ARGUMENT);

  g_assert_cmpint(g_remove(non_executable), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_signal_termination(void) {
  const char *values[] = {self_path, "--helper", "signal"};
  ShaulaProcessSpan spans[G_N_ELEMENTS(values)];
  ShaulaProcessOutput output;

  g_assert_cmpint(
      shaula_process_run(argv_from_strings(values, G_N_ELEMENTS(values), spans),
                         NULL, 0, 0, &output),
      ==, SHAULA_PROCESS_STATUS_OK);
  g_assert_cmpint(output.term_kind, ==, SHAULA_PROCESS_TERM_SIGNAL);
  g_assert_cmpuint(output.term_value, ==, SIGTERM);
  g_assert_cmpuint(output.stdout_bytes.length, ==, 0);
  g_assert_cmpuint(output.stderr_bytes.length, ==, 0);
  shaula_process_output_clear(&output);
}

static void test_pipe_input_binary_exit_and_sigpipe(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-process-input-XXXXXX", &error);
  g_autofree char *path = NULL;
  g_autofree char *loaded = NULL;
  g_autofree char *large_input = g_malloc(1024 * 1024);
  gsize loaded_length = 0;
  static const char input[] = {'a', '\0', 'b', '\n', 'c'};
  const char *copy_values[5];
  ShaulaProcessSpan copy_spans[5];
  const char *close_values[] = {self_path, "--helper", "close-stdin"};
  ShaulaProcessSpan close_spans[G_N_ELEMENTS(close_values)];
  const char *missing_values[] = {
      "shaula-process-input-command-that-does-not-exist-e2f4"};
  ShaulaProcessSpan missing_spans[G_N_ELEMENTS(missing_values)];
  ShaulaProcessTermKind term_kind;
  uint32_t term_value;

  g_assert_no_error(error);
  g_assert_nonnull(root);
  path = g_build_filename(root, "input.bin", NULL);
  memset(large_input, 'z', 1024 * 1024);

  copy_values[0] = self_path;
  copy_values[1] = "--helper";
  copy_values[2] = "copy-stdin";
  copy_values[3] = path;
  copy_values[4] = "9";
  g_assert_cmpint(
      shaula_process_run_with_input(
          argv_from_strings(copy_values, G_N_ELEMENTS(copy_values), copy_spans),
          (ShaulaProcessSpan){input, sizeof(input)}, &term_kind, &term_value),
      ==, SHAULA_PROCESS_STATUS_OK);
  g_assert_cmpint(term_kind, ==, SHAULA_PROCESS_TERM_EXITED);
  g_assert_cmpuint(term_value, ==, 9);
  g_assert_true(g_file_get_contents(path, &loaded, &loaded_length, &error));
  g_assert_no_error(error);
  g_assert_cmpuint(loaded_length, ==, sizeof(input));
  g_assert_cmpmem(loaded, loaded_length, input, sizeof(input));

  g_assert_cmpint(
      shaula_process_run_with_input(
          argv_from_strings(close_values, G_N_ELEMENTS(close_values),
                            close_spans),
          (ShaulaProcessSpan){large_input, 1024 * 1024}, &term_kind,
          &term_value),
      ==, SHAULA_PROCESS_STATUS_IO_ERROR);

  g_assert_cmpint(
      shaula_process_run_with_input(
          argv_from_strings(missing_values, G_N_ELEMENTS(missing_values),
                            missing_spans),
          (ShaulaProcessSpan){NULL, 0}, &term_kind, &term_value),
      ==, SHAULA_PROCESS_STATUS_FILE_NOT_FOUND);
  g_assert_cmpint(
      shaula_process_run_with_input(
          argv_from_strings(copy_values, G_N_ELEMENTS(copy_values), copy_spans),
          (ShaulaProcessSpan){NULL, 1}, &term_kind, &term_value),
      ==, SHAULA_PROCESS_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(
      shaula_process_run_with_input(
          argv_from_strings(copy_values, G_N_ELEMENTS(copy_values), copy_spans),
          (ShaulaProcessSpan){NULL, 0}, NULL, &term_value),
      ==, SHAULA_PROCESS_STATUS_INVALID_ARGUMENT);

  g_assert_cmpint(g_remove(path), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

int main(int argc, char **argv) {
  int helper_result = helper_main(argc, argv);

  if (helper_result >= 0) {
    return helper_result;
  }

  self_path = g_file_read_link("/proc/self/exe", NULL);
  if (self_path == NULL) {
    return 132;
  }

  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/runtime/process/binary-output",
                  test_binary_output_exit_and_literal_argv);
  g_test_add_func("/runtime/process/dual-stream-and-limits",
                  test_dual_stream_drain_and_limits);
  g_test_add_func("/runtime/process/replacement-environment",
                  test_replacement_environment_uses_parent_path);
  g_test_add_func("/runtime/process/path-and-exec-parity",
                  test_parent_path_and_exec_parity);
  g_test_add_func("/runtime/process/spawn-errors",
                  test_spawn_errors_and_invalid_arguments);
  g_test_add_func("/runtime/process/signal", test_signal_termination);
  g_test_add_func("/runtime/process/pipe-input",
                  test_pipe_input_binary_exit_and_sigpipe);

  helper_result = g_test_run();
  g_free(self_path);
  return helper_result;
}
