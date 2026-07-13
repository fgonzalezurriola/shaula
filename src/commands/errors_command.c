#include "commands.h"

#include "errors/taxonomy.h"
#include "support.h"

#include <glib.h>

int shaula_errors_command_run(int argc, char **argv) {
  if (argc != 4 || !g_str_equal(argv[2], "list") ||
      !g_str_equal(argv[3], "--json"))
    return shaula_command_write_error("errors list", "ERR_CLI_USAGE",
                                      "usage: shaula errors list --json",
                                      "{}");

  g_autofree char *timestamp = shaula_command_json_timestamp();
  GString *output = g_string_new(NULL);
  g_string_append_printf(
      output,
      "{\"ok\":true,\"contract_version\":\"1.0.0\",\"command\":"
      "\"errors list\",\"timestamp\":\"%s\",\"result\":{\"errors\":[",
      timestamp);
  for (size_t i = 0; i < shaula_error_taxonomy_count(); i++) {
    const ShaulaErrorSpec *spec = shaula_error_taxonomy_at(i);
    ShaulaErrorSpan class_token =
        shaula_failure_class_token(spec->failure_class);
    ShaulaErrorSpan action_token = shaula_recovery_action_token(spec->action);
    g_autofree char *code = g_strndup(spec->code.data, spec->code.length);
    g_autofree char *message =
        g_strndup(spec->message.data, spec->message.length);
    g_autofree char *class_text =
        g_strndup(class_token.data, class_token.length);
    g_autofree char *action =
        g_strndup(action_token.data, action_token.length);
    g_autofree char *code_json = shaula_command_json_string(code);
    g_autofree char *message_json = shaula_command_json_string(message);
    g_autofree char *class_json = shaula_command_json_string(class_text);
    g_autofree char *action_json = shaula_command_json_string(action);
    if (i > 0)
      g_string_append_c(output, ',');
    g_string_append_printf(
        output,
        "{\"code\":%s,\"message\":%s,\"retryable\":%s,\"class\":%s,"
        "\"action\":%s,\"exit_code\":%u}",
        code_json, message_json,
        shaula_command_json_bool(spec->retryable), class_json, action_json,
        spec->exit_code);
  }
  g_string_append(output, "]},\"warnings\":[]}\n");
  g_print("%s", output->str);
  g_string_free(output, TRUE);
  return 0;
}
