#include "preflight/probe.h"

#include "errors/taxonomy.h"

#include <glib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LITERAL_SPAN(value)                                                     \
  ((ShaulaJsonSpan){(const uint8_t *)(value), sizeof(value) - 1U})
#define ERROR_LITERAL_SPAN(value)                                               \
  ((ShaulaErrorSpan){(value), sizeof(value) - 1U})

typedef struct {
  uint8_t *data;
  size_t length;
  size_t capacity;
} PreflightBuilder;

static gboolean span_is_valid(ShaulaJsonSpan span) {
  return span.data != NULL || span.length == 0U;
}

static gboolean checked_add_size(size_t left, size_t right, size_t *result) {
  if (right > SIZE_MAX - left) {
    return FALSE;
  }
  *result = left + right;
  return TRUE;
}

static ShaulaPreflightStatus builder_reserve(PreflightBuilder *builder,
                                              size_t additional) {
  size_t required_without_nul = 0U;
  size_t required = 0U;
  size_t new_capacity = 0U;
  uint8_t *new_data = NULL;

  if (!checked_add_size(builder->length, additional, &required_without_nul) ||
      !checked_add_size(required_without_nul, 1U, &required)) {
    return SHAULA_PREFLIGHT_STATUS_SIZE_OVERFLOW;
  }
  if (required <= builder->capacity) {
    return SHAULA_PREFLIGHT_STATUS_OK;
  }

  new_capacity = builder->capacity == 0U ? 128U : builder->capacity;
  while (new_capacity < required) {
    if (new_capacity > SIZE_MAX / 2U) {
      new_capacity = required;
      break;
    }
    new_capacity *= 2U;
  }

  new_data = g_try_realloc(builder->data, new_capacity);
  if (new_data == NULL) {
    return SHAULA_PREFLIGHT_STATUS_OUT_OF_MEMORY;
  }
  builder->data = new_data;
  builder->capacity = new_capacity;
  return SHAULA_PREFLIGHT_STATUS_OK;
}

static ShaulaPreflightStatus builder_append(PreflightBuilder *builder,
                                             const uint8_t *data,
                                             size_t length) {
  ShaulaPreflightStatus status = SHAULA_PREFLIGHT_STATUS_OK;

  if (data == NULL && length != 0U) {
    return SHAULA_PREFLIGHT_STATUS_INVALID_ARGUMENT;
  }
  status = builder_reserve(builder, length);
  if (status != SHAULA_PREFLIGHT_STATUS_OK) {
    return status;
  }
  if (length != 0U) {
    memcpy(builder->data + builder->length, data, length);
    builder->length += length;
  }
  return SHAULA_PREFLIGHT_STATUS_OK;
}

static ShaulaPreflightStatus builder_append_span(PreflightBuilder *builder,
                                                  ShaulaJsonSpan span) {
  if (!span_is_valid(span)) {
    return SHAULA_PREFLIGHT_STATUS_INVALID_ARGUMENT;
  }
  return builder_append(builder, span.data, span.length);
}

static ShaulaPreflightStatus builder_append_literal(PreflightBuilder *builder,
                                                     const char *literal) {
  return builder_append(builder, (const uint8_t *)literal, strlen(literal));
}

static ShaulaPreflightStatus
builder_finish(PreflightBuilder *builder, ShaulaJsonOwnedBytes *output) {
  ShaulaPreflightStatus status = builder_reserve(builder, 0U);

  if (status != SHAULA_PREFLIGHT_STATUS_OK) {
    return status;
  }
  builder->data[builder->length] = 0U;
  output->data = builder->data;
  output->length = builder->length;
  builder->data = NULL;
  builder->length = 0U;
  builder->capacity = 0U;
  return SHAULA_PREFLIGHT_STATUS_OK;
}

static void builder_clear(PreflightBuilder *builder) {
  g_free(builder->data);
  builder->data = NULL;
  builder->length = 0U;
  builder->capacity = 0U;
}

static ShaulaPreflightStatus map_json_status(ShaulaJsonStatus status) {
  switch (status) {
  case SHAULA_JSON_STATUS_OK:
    return SHAULA_PREFLIGHT_STATUS_OK;
  case SHAULA_JSON_STATUS_INVALID_ARGUMENT:
    return SHAULA_PREFLIGHT_STATUS_INVALID_ARGUMENT;
  case SHAULA_JSON_STATUS_SIZE_OVERFLOW:
    return SHAULA_PREFLIGHT_STATUS_SIZE_OVERFLOW;
  case SHAULA_JSON_STATUS_OUT_OF_MEMORY:
    return SHAULA_PREFLIGHT_STATUS_OUT_OF_MEMORY;
  case SHAULA_JSON_STATUS_TIMESTAMP_OUT_OF_RANGE:
    return SHAULA_PREFLIGHT_STATUS_TIMESTAMP_OUT_OF_RANGE;
  default:
    return SHAULA_PREFLIGHT_STATUS_INTERNAL_ERROR;
  }
}

static ShaulaJsonSpan env_span_as_json(ShaulaEnvSpan span) {
  return (ShaulaJsonSpan){(const uint8_t *)span.data, span.length};
}

static ShaulaPreflightStatus build_error(
    int64_t unix_seconds, const char *code, size_t code_length,
    const char *message, size_t message_length, uint8_t retryable,
    ShaulaEnvSpan compositor, ShaulaPreflightOutput *output) {
  ShaulaJsonOwnedBytes escaped_compositor = {0};
  ShaulaJsonOwnedBytes details = {0};
  PreflightBuilder builder = {0};
  ShaulaPreflightStatus status = SHAULA_PREFLIGHT_STATUS_OK;
  ShaulaJsonStatus json_status = SHAULA_JSON_STATUS_OK;

  json_status = shaula_json_string_escape(env_span_as_json(compositor),
                                          &escaped_compositor);
  status = map_json_status(json_status);
  if (status == SHAULA_PREFLIGHT_STATUS_OK) {
    status = builder_append_literal(&builder, "{\"detected_compositor\":");
  }
  if (status == SHAULA_PREFLIGHT_STATUS_OK) {
    status = builder_append(&builder, escaped_compositor.data,
                            escaped_compositor.length);
  }
  if (status == SHAULA_PREFLIGHT_STATUS_OK) {
    status = builder_append_literal(&builder, "}");
  }
  if (status == SHAULA_PREFLIGHT_STATUS_OK) {
    status = builder_finish(&builder, &details);
  }
  if (status == SHAULA_PREFLIGHT_STATUS_OK) {
    json_status = shaula_json_basic_error_build(
        unix_seconds, LITERAL_SPAN("preflight"),
        (ShaulaJsonSpan){(const uint8_t *)code, code_length},
        (ShaulaJsonSpan){(const uint8_t *)message, message_length}, retryable,
        (ShaulaJsonSpan){details.data, details.length}, &output->json);
    status = map_json_status(json_status);
  }
  if (status == SHAULA_PREFLIGHT_STATUS_OK) {
    output->exit_code = shaula_error_exit_code_for(
        (ShaulaErrorSpan){code, code_length});
  }

  builder_clear(&builder);
  shaula_json_owned_bytes_clear(&escaped_compositor);
  shaula_json_owned_bytes_clear(&details);
  return status;
}

static ShaulaPreflightStatus build_success(
    int64_t unix_seconds, ShaulaJsonSpan portal_fallback_warning,
    const ShaulaRuntimeDecision *runtime, ShaulaPreflightOutput *output) {
  ShaulaJsonOwnedBytes timestamp = {0};
  ShaulaJsonOwnedBytes warnings = {0};
  PreflightBuilder builder = {0};
  ShaulaPreflightStatus status = SHAULA_PREFLIGHT_STATUS_OK;
  ShaulaJsonStatus json_status = SHAULA_JSON_STATUS_OK;
  const ShaulaJsonSpan contract_version = shaula_json_contract_version();
  const ShaulaJsonSpan compositor = env_span_as_json(runtime->compositor.label);
  const ShaulaJsonSpan backend =
      env_span_as_json(shaula_capabilities_backend_label(runtime->backend));
  const int32_t uses_portal =
      shaula_capabilities_uses_portal_backend(*runtime);

  if (!span_is_valid(contract_version) || !span_is_valid(compositor) ||
      !span_is_valid(backend) || uses_portal < 0) {
    return SHAULA_PREFLIGHT_STATUS_INTERNAL_ERROR;
  }

  json_status =
      shaula_json_timestamp_from_unix_seconds(unix_seconds, &timestamp);
  status = map_json_status(json_status);
  if (status == SHAULA_PREFLIGHT_STATUS_OK) {
    json_status = shaula_json_warnings_serialize(
        uses_portal == 1 ? &portal_fallback_warning : NULL,
        uses_portal == 1 ? 1U : 0U, &warnings);
    status = map_json_status(json_status);
  }

#define APPEND_LITERAL(value)                                                   \
  do {                                                                          \
    if (status == SHAULA_PREFLIGHT_STATUS_OK) {                                 \
      status = builder_append_literal(&builder, (value));                       \
    }                                                                           \
  } while (0)
#define APPEND_SPAN(value)                                                      \
  do {                                                                          \
    if (status == SHAULA_PREFLIGHT_STATUS_OK) {                                 \
      status = builder_append_span(&builder, (value));                          \
    }                                                                           \
  } while (0)

  APPEND_LITERAL("{\"ok\":true,\"contract_version\":\"");
  APPEND_SPAN(contract_version);
  APPEND_LITERAL("\",\"command\":\"preflight\",\"timestamp\":\"");
  APPEND_SPAN(((ShaulaJsonSpan){timestamp.data, timestamp.length}));
  APPEND_LITERAL("\",\"compositor\":\"");
  APPEND_SPAN(compositor);
  APPEND_LITERAL("\",\"ready\":true,\"result\":{\"compositor\":\"");
  APPEND_SPAN(compositor);
  APPEND_LITERAL("\",\"wayland\":true,\"backend\":\"");
  APPEND_SPAN(backend);
  APPEND_LITERAL("\",\"portal_available\":");
  APPEND_LITERAL(runtime->portal_available != 0 ? "true" : "false");
  APPEND_LITERAL("},\"warnings\":");
  APPEND_SPAN(((ShaulaJsonSpan){warnings.data, warnings.length}));
  APPEND_LITERAL("}\n");

#undef APPEND_SPAN
#undef APPEND_LITERAL

  if (status == SHAULA_PREFLIGHT_STATUS_OK) {
    status = builder_finish(&builder, &output->json);
  }
  if (status == SHAULA_PREFLIGHT_STATUS_OK) {
    output->exit_code = 0U;
  }

  builder_clear(&builder);
  shaula_json_owned_bytes_clear(&timestamp);
  shaula_json_owned_bytes_clear(&warnings);
  return status;
}

void shaula_preflight_output_init(ShaulaPreflightOutput *output) {
  if (output == NULL) {
    return;
  }
  shaula_json_owned_bytes_init(&output->json);
  output->exit_code = 0U;
}

void shaula_preflight_output_clear(ShaulaPreflightOutput *output) {
  if (output == NULL) {
    return;
  }
  shaula_json_owned_bytes_clear(&output->json);
  output->exit_code = 0U;
}

ShaulaPreflightStatus shaula_preflight_build(
    const ShaulaCapabilitiesEnvironment *environment,
    int64_t unix_seconds,
    ShaulaJsonSpan portal_fallback_warning,
    ShaulaPreflightOutput *output) {
  ShaulaRuntimeDecision runtime = {0};
  ShaulaPreflightStatus status = SHAULA_PREFLIGHT_STATUS_OK;

  if (output == NULL) {
    return SHAULA_PREFLIGHT_STATUS_INVALID_ARGUMENT;
  }
  shaula_preflight_output_clear(output);
  if (environment == NULL || !span_is_valid(portal_fallback_warning)) {
    return SHAULA_PREFLIGHT_STATUS_INVALID_ARGUMENT;
  }
  if (shaula_capabilities_resolve(environment, &runtime) !=
      SHAULA_CAPABILITIES_STATUS_OK) {
    return SHAULA_PREFLIGHT_STATUS_INTERNAL_ERROR;
  }

  if (runtime.compositor_supported == 0) {
    status = build_error(
        unix_seconds, "ERR_UNSUPPORTED_COMPOSITOR",
        sizeof("ERR_UNSUPPORTED_COMPOSITOR") - 1U,
        "unsupported compositor for shaula v1",
        sizeof("unsupported compositor for shaula v1") - 1U, 0U,
        runtime.compositor.label, output);
  } else if (environment->compositor.wayland_display == NULL) {
    status = build_error(
        unix_seconds, "ERR_PREFLIGHT_ENV_NOT_READY",
        sizeof("ERR_PREFLIGHT_ENV_NOT_READY") - 1U,
        "wayland environment is not ready",
        sizeof("wayland environment is not ready") - 1U, 1U,
        runtime.compositor.label, output);
  } else {
    status = build_success(unix_seconds, portal_fallback_warning, &runtime,
                           output);
  }

  if (status != SHAULA_PREFLIGHT_STATUS_OK) {
    shaula_preflight_output_clear(output);
  }
  return status;
}
