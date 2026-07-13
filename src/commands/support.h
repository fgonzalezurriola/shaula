#ifndef SHAULA_COMMANDS_SUPPORT_H
#define SHAULA_COMMANDS_SUPPORT_H

#include "capabilities/runtime.h"
#include "cli/json.h"
#include "runtime/env.h"

#include <glib.h>

ShaulaJsonSpan shaula_command_json_span(const char *value);
char *shaula_command_json_string(const char *value);
char *shaula_command_json_timestamp(void);
const char *shaula_command_json_bool(gboolean value);

int shaula_command_write_error(const char *command, const char *code,
                               const char *message,
                               const char *details_json);
int shaula_command_write_success(const char *command,
                                 const char *result_json,
                                 const char *warnings_json);

ShaulaCapabilitiesEnvironment shaula_command_capabilities_environment(void);
char *shaula_command_env_span_json(ShaulaEnvSpan span);

#endif
