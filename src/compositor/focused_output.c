#include "compositor/focused_output.h"

#include "runtime/env.h"
#include "runtime/process_exec.h"

#include <glib.h>
#include <string.h>

typedef enum {
  FOCUSED_JSON_STATUS_OK,
  FOCUSED_JSON_STATUS_INVALID,
  FOCUSED_JSON_STATUS_OUT_OF_MEMORY,
} FocusedJsonStatus;

typedef enum {
  FOCUSED_JSON_VALUE_NULL,
  FOCUSED_JSON_VALUE_BOOL,
  FOCUSED_JSON_VALUE_STRING,
  FOCUSED_JSON_VALUE_OTHER,
} FocusedJsonValueKind;

typedef struct {
  size_t start;
  size_t end;
} FocusedJsonRawString;

typedef struct {
  const uint8_t *data;
  size_t length;
  size_t position;
  size_t depth;
  uint8_t *scratch;
  size_t scratch_capacity;
} FocusedJsonParser;

typedef struct {
  FocusedJsonValueKind kind;
  gboolean bool_value;
  const uint8_t *string_data;
  size_t string_length;
} FocusedJsonValue;

static FocusedJsonStatus focused_json_parse_value(FocusedJsonParser *parser,
                                                  FocusedJsonValue *value);

static gboolean focused_json_is_whitespace(uint8_t value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static void focused_json_skip_whitespace(FocusedJsonParser *parser) {
  while (parser->position < parser->length &&
         focused_json_is_whitespace(parser->data[parser->position])) {
    parser->position += 1U;
  }
}

static gboolean focused_json_consume_byte(FocusedJsonParser *parser,
                                          uint8_t expected) {
  if (parser->position >= parser->length ||
      parser->data[parser->position] != expected) {
    return FALSE;
  }
  parser->position += 1U;
  return TRUE;
}

static gboolean focused_json_consume_literal(FocusedJsonParser *parser,
                                             const char *literal) {
  const size_t literal_length = strlen(literal);
  if (literal_length > parser->length - parser->position) {
    return FALSE;
  }
  if (memcmp(parser->data + parser->position, literal, literal_length) != 0) {
    return FALSE;
  }
  parser->position += literal_length;
  return TRUE;
}

static int focused_json_hex_value(uint8_t value) {
  if (value >= '0' && value <= '9') {
    return (int)(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return 10 + (int)(value - 'a');
  }
  if (value >= 'A' && value <= 'F') {
    return 10 + (int)(value - 'A');
  }
  return -1;
}

static gboolean focused_json_parse_hex_quad(FocusedJsonParser *parser,
                                            guint32 *value_out) {
  guint32 value = 0U;
  size_t index;

  if (parser->length - parser->position < 4U) {
    return FALSE;
  }
  for (index = 0U; index < 4U; index += 1U) {
    const int digit = focused_json_hex_value(parser->data[parser->position]);
    if (digit < 0) {
      return FALSE;
    }
    value = (value << 4U) | (guint32)digit;
    parser->position += 1U;
  }
  *value_out = value;
  return TRUE;
}

static gboolean focused_json_append_bytes(uint8_t *output,
                                          size_t output_capacity,
                                          size_t *output_length,
                                          const uint8_t *data, size_t length) {
  if (length > output_capacity - *output_length) {
    return FALSE;
  }
  if (length != 0U) {
    memcpy(output + *output_length, data, length);
  }
  *output_length += length;
  return TRUE;
}

static gboolean focused_json_append_codepoint(uint8_t *output,
                                              size_t output_capacity,
                                              size_t *output_length,
                                              gunichar codepoint) {
  gchar encoded[6];
  gint encoded_length;

  if (!g_unichar_validate(codepoint)) {
    return FALSE;
  }
  encoded_length = g_unichar_to_utf8(codepoint, encoded);
  if (encoded_length <= 0) {
    return FALSE;
  }
  return focused_json_append_bytes(output, output_capacity, output_length,
                                   (const uint8_t *)encoded,
                                   (size_t)encoded_length);
}

static size_t focused_json_utf8_sequence_length(uint8_t first) {
  if (first >= 0xc2U && first <= 0xdfU) {
    return 2U;
  }
  if (first >= 0xe0U && first <= 0xefU) {
    return 3U;
  }
  if (first >= 0xf0U && first <= 0xf4U) {
    return 4U;
  }
  return 0U;
}

static gboolean focused_json_append_raw_utf8(FocusedJsonParser *parser,
                                             uint8_t *output,
                                             size_t output_capacity,
                                             size_t *output_length) {
  const size_t sequence_length =
      focused_json_utf8_sequence_length(parser->data[parser->position]);
  const gchar *sequence;
  gunichar codepoint;

  if (sequence_length == 0U ||
      sequence_length > parser->length - parser->position) {
    return FALSE;
  }
  sequence = (const gchar *)(parser->data + parser->position);
  codepoint = g_utf8_get_char_validated(sequence, (gssize)sequence_length);
  if (codepoint == (gunichar)-1 || codepoint == (gunichar)-2 ||
      !g_unichar_validate(codepoint)) {
    return FALSE;
  }
  if (!focused_json_append_bytes(output, output_capacity, output_length,
                                 parser->data + parser->position,
                                 sequence_length)) {
    return FALSE;
  }
  parser->position += sequence_length;
  return TRUE;
}

static gboolean focused_json_append_escape(FocusedJsonParser *parser,
                                           uint8_t *output,
                                           size_t output_capacity,
                                           size_t *output_length) {
  uint8_t escaped;

  if (parser->position >= parser->length) {
    return FALSE;
  }
  escaped = parser->data[parser->position];
  parser->position += 1U;

  switch (escaped) {
  case '"':
  case '\\':
  case '/':
    return focused_json_append_bytes(output, output_capacity, output_length,
                                     &escaped, 1U);
  case 'b': {
    const uint8_t value = '\b';
    return focused_json_append_bytes(output, output_capacity, output_length,
                                     &value, 1U);
  }
  case 'f': {
    const uint8_t value = '\f';
    return focused_json_append_bytes(output, output_capacity, output_length,
                                     &value, 1U);
  }
  case 'n': {
    const uint8_t value = '\n';
    return focused_json_append_bytes(output, output_capacity, output_length,
                                     &value, 1U);
  }
  case 'r': {
    const uint8_t value = '\r';
    return focused_json_append_bytes(output, output_capacity, output_length,
                                     &value, 1U);
  }
  case 't': {
    const uint8_t value = '\t';
    return focused_json_append_bytes(output, output_capacity, output_length,
                                     &value, 1U);
  }
  case 'u': {
    guint32 first = 0U;
    gunichar codepoint;

    if (!focused_json_parse_hex_quad(parser, &first)) {
      return FALSE;
    }
    codepoint = (gunichar)first;
    if (first >= 0xd800U && first <= 0xdbffU) {
      guint32 second = 0U;
      if (parser->length - parser->position < 6U ||
          parser->data[parser->position] != '\\' ||
          parser->data[parser->position + 1U] != 'u') {
        return FALSE;
      }
      parser->position += 2U;
      if (!focused_json_parse_hex_quad(parser, &second) || second < 0xdc00U ||
          second > 0xdfffU) {
        return FALSE;
      }
      codepoint = (gunichar)(0x10000U + ((first - 0xd800U) << 10U) +
                             (second - 0xdc00U));
    } else if (first >= 0xdc00U && first <= 0xdfffU) {
      return FALSE;
    }
    return focused_json_append_codepoint(output, output_capacity, output_length,
                                         codepoint);
  }
  default:
    return FALSE;
  }
}

static FocusedJsonStatus focused_json_parse_string_into(
    FocusedJsonParser *parser, uint8_t *output, size_t output_capacity,
    size_t *output_length, FocusedJsonRawString *raw) {
  size_t length = 0U;

  if (!focused_json_consume_byte(parser, '"')) {
    return FOCUSED_JSON_STATUS_INVALID;
  }
  if (raw != NULL) {
    raw->start = parser->position - 1U;
    raw->end = 0U;
  }

  while (parser->position < parser->length) {
    const uint8_t current = parser->data[parser->position];
    if (current == '"') {
      parser->position += 1U;
      if (length >= output_capacity) {
        return FOCUSED_JSON_STATUS_INVALID;
      }
      output[length] = 0U;
      *output_length = length;
      if (raw != NULL) {
        raw->end = parser->position;
      }
      return FOCUSED_JSON_STATUS_OK;
    }
    if (current < 0x20U) {
      return FOCUSED_JSON_STATUS_INVALID;
    }
    if (current == '\\') {
      parser->position += 1U;
      if (!focused_json_append_escape(parser, output, output_capacity,
                                      &length)) {
        return FOCUSED_JSON_STATUS_INVALID;
      }
      continue;
    }
    if (current < 0x80U) {
      if (!focused_json_append_bytes(output, output_capacity, &length, &current,
                                     1U)) {
        return FOCUSED_JSON_STATUS_INVALID;
      }
      parser->position += 1U;
      continue;
    }
    if (!focused_json_append_raw_utf8(parser, output, output_capacity, &length)) {
      return FOCUSED_JSON_STATUS_INVALID;
    }
  }
  return FOCUSED_JSON_STATUS_INVALID;
}

static gboolean focused_json_parse_number(FocusedJsonParser *parser) {
  size_t position = parser->position;

  if (position < parser->length && parser->data[position] == '-') {
    position += 1U;
  }
  if (position >= parser->length) {
    return FALSE;
  }
  if (parser->data[position] == '0') {
    position += 1U;
  } else if (parser->data[position] >= '1' &&
             parser->data[position] <= '9') {
    position += 1U;
    while (position < parser->length && parser->data[position] >= '0' &&
           parser->data[position] <= '9') {
      position += 1U;
    }
  } else {
    return FALSE;
  }

  if (position < parser->length && parser->data[position] == '.') {
    position += 1U;
    if (position >= parser->length || parser->data[position] < '0' ||
        parser->data[position] > '9') {
      return FALSE;
    }
    while (position < parser->length && parser->data[position] >= '0' &&
           parser->data[position] <= '9') {
      position += 1U;
    }
  }

  if (position < parser->length &&
      (parser->data[position] == 'e' || parser->data[position] == 'E')) {
    position += 1U;
    if (position < parser->length &&
        (parser->data[position] == '+' || parser->data[position] == '-')) {
      position += 1U;
    }
    if (position >= parser->length || parser->data[position] < '0' ||
        parser->data[position] > '9') {
      return FALSE;
    }
    while (position < parser->length && parser->data[position] >= '0' &&
           parser->data[position] <= '9') {
      position += 1U;
    }
  }

  parser->position = position;
  return TRUE;
}

static FocusedJsonStatus
focused_json_parse_generic_array(FocusedJsonParser *parser) {
  FocusedJsonStatus status = FOCUSED_JSON_STATUS_INVALID;

  if (parser->depth >= 256U || !focused_json_consume_byte(parser, '[')) {
    return FOCUSED_JSON_STATUS_INVALID;
  }
  parser->depth += 1U;
  focused_json_skip_whitespace(parser);
  if (focused_json_consume_byte(parser, ']')) {
    status = FOCUSED_JSON_STATUS_OK;
    goto cleanup;
  }

  while (TRUE) {
    FocusedJsonValue value = {0};
    status = focused_json_parse_value(parser, &value);
    if (status != FOCUSED_JSON_STATUS_OK) {
      goto cleanup;
    }
    focused_json_skip_whitespace(parser);
    if (focused_json_consume_byte(parser, ']')) {
      status = FOCUSED_JSON_STATUS_OK;
      goto cleanup;
    }
    if (!focused_json_consume_byte(parser, ',')) {
      status = FOCUSED_JSON_STATUS_INVALID;
      goto cleanup;
    }
    focused_json_skip_whitespace(parser);
  }

cleanup:
  parser->depth -= 1U;
  return status;
}

static FocusedJsonStatus
focused_json_parse_generic_object(FocusedJsonParser *parser) {
  FocusedJsonStatus status = FOCUSED_JSON_STATUS_INVALID;

  if (parser->depth >= 256U || !focused_json_consume_byte(parser, '{')) {
    return FOCUSED_JSON_STATUS_INVALID;
  }
  parser->depth += 1U;
  focused_json_skip_whitespace(parser);
  if (focused_json_consume_byte(parser, '}')) {
    status = FOCUSED_JSON_STATUS_OK;
    goto cleanup;
  }

  while (TRUE) {
    size_t key_length = 0U;
    FocusedJsonValue value = {0};

    status = focused_json_parse_string_into(
        parser, parser->scratch, parser->scratch_capacity, &key_length, NULL);
    if (status != FOCUSED_JSON_STATUS_OK) {
      goto cleanup;
    }
    focused_json_skip_whitespace(parser);
    if (!focused_json_consume_byte(parser, ':')) {
      status = FOCUSED_JSON_STATUS_INVALID;
      goto cleanup;
    }
    focused_json_skip_whitespace(parser);
    status = focused_json_parse_value(parser, &value);
    if (status != FOCUSED_JSON_STATUS_OK) {
      goto cleanup;
    }
    focused_json_skip_whitespace(parser);
    if (focused_json_consume_byte(parser, '}')) {
      status = FOCUSED_JSON_STATUS_OK;
      goto cleanup;
    }
    if (!focused_json_consume_byte(parser, ',')) {
      status = FOCUSED_JSON_STATUS_INVALID;
      goto cleanup;
    }
    focused_json_skip_whitespace(parser);
  }

cleanup:
  parser->depth -= 1U;
  return status;
}

static FocusedJsonStatus focused_json_parse_value(FocusedJsonParser *parser,
                                                  FocusedJsonValue *value) {
  value->kind = FOCUSED_JSON_VALUE_OTHER;
  value->bool_value = FALSE;
  value->string_data = NULL;
  value->string_length = 0U;

  focused_json_skip_whitespace(parser);
  if (parser->position >= parser->length) {
    return FOCUSED_JSON_STATUS_INVALID;
  }

  switch (parser->data[parser->position]) {
  case 'n':
    if (!focused_json_consume_literal(parser, "null")) {
      return FOCUSED_JSON_STATUS_INVALID;
    }
    value->kind = FOCUSED_JSON_VALUE_NULL;
    return FOCUSED_JSON_STATUS_OK;
  case 't':
    if (!focused_json_consume_literal(parser, "true")) {
      return FOCUSED_JSON_STATUS_INVALID;
    }
    value->kind = FOCUSED_JSON_VALUE_BOOL;
    value->bool_value = TRUE;
    return FOCUSED_JSON_STATUS_OK;
  case 'f':
    if (!focused_json_consume_literal(parser, "false")) {
      return FOCUSED_JSON_STATUS_INVALID;
    }
    value->kind = FOCUSED_JSON_VALUE_BOOL;
    value->bool_value = FALSE;
    return FOCUSED_JSON_STATUS_OK;
  case '"': {
    size_t length = 0U;
    const FocusedJsonStatus status = focused_json_parse_string_into(
        parser, parser->scratch, parser->scratch_capacity, &length, NULL);
    if (status != FOCUSED_JSON_STATUS_OK) {
      return status;
    }
    value->kind = FOCUSED_JSON_VALUE_STRING;
    value->string_data = parser->scratch;
    value->string_length = length;
    return FOCUSED_JSON_STATUS_OK;
  }
  case '{':
    return focused_json_parse_generic_object(parser);
  case '[':
    return focused_json_parse_generic_array(parser);
  default:
    if (!focused_json_parse_number(parser)) {
      return FOCUSED_JSON_STATUS_INVALID;
    }
    return FOCUSED_JSON_STATUS_OK;
  }
}

static gboolean focused_bytes_equal(const uint8_t *data, size_t length,
                                    const char *literal) {
  const size_t literal_length = strlen(literal);
  return length == literal_length &&
         (length == 0U || memcmp(data, literal, length) == 0);
}

static FocusedJsonStatus focused_json_parse_niri_root(
    FocusedJsonParser *parser, FocusedJsonRawString *selected_name,
    gboolean *has_selected_name) {
  gboolean name_seen = FALSE;
  FocusedJsonStatus status = FOCUSED_JSON_STATUS_INVALID;

  *has_selected_name = FALSE;
  if (parser->depth >= 256U || !focused_json_consume_byte(parser, '{')) {
    return FOCUSED_JSON_STATUS_INVALID;
  }
  parser->depth += 1U;
  focused_json_skip_whitespace(parser);
  if (focused_json_consume_byte(parser, '}')) {
    goto cleanup;
  }

  while (TRUE) {
    size_t key_length = 0U;
    gboolean is_name;

    status = focused_json_parse_string_into(
        parser, parser->scratch, parser->scratch_capacity, &key_length, NULL);
    if (status != FOCUSED_JSON_STATUS_OK) {
      goto cleanup;
    }
    is_name = focused_bytes_equal(parser->scratch, key_length, "name");
    if (is_name && name_seen) {
      status = FOCUSED_JSON_STATUS_INVALID;
      goto cleanup;
    }

    focused_json_skip_whitespace(parser);
    if (!focused_json_consume_byte(parser, ':')) {
      status = FOCUSED_JSON_STATUS_INVALID;
      goto cleanup;
    }
    focused_json_skip_whitespace(parser);

    if (is_name) {
      size_t name_length = 0U;
      status = focused_json_parse_string_into(
          parser, parser->scratch, parser->scratch_capacity, &name_length,
          selected_name);
      if (status != FOCUSED_JSON_STATUS_OK) {
        goto cleanup;
      }
      name_seen = TRUE;
      *has_selected_name = name_length != 0U;
    } else {
      FocusedJsonValue value = {0};
      status = focused_json_parse_value(parser, &value);
      if (status != FOCUSED_JSON_STATUS_OK) {
        goto cleanup;
      }
    }

    focused_json_skip_whitespace(parser);
    if (focused_json_consume_byte(parser, '}')) {
      status = name_seen ? FOCUSED_JSON_STATUS_OK
                         : FOCUSED_JSON_STATUS_INVALID;
      goto cleanup;
    }
    if (!focused_json_consume_byte(parser, ',')) {
      status = FOCUSED_JSON_STATUS_INVALID;
      goto cleanup;
    }
    focused_json_skip_whitespace(parser);
  }

cleanup:
  parser->depth -= 1U;
  return status;
}

static FocusedJsonStatus focused_json_parse_sway_item(
    FocusedJsonParser *parser, FocusedJsonRawString *selected_name,
    gboolean *has_selected_name) {
  gboolean name_seen = FALSE;
  gboolean focused_seen = FALSE;
  gboolean focused = FALSE;
  size_t name_length = 0U;
  FocusedJsonRawString name_raw = {0};
  FocusedJsonStatus status = FOCUSED_JSON_STATUS_INVALID;

  if (parser->depth >= 256U || !focused_json_consume_byte(parser, '{')) {
    return FOCUSED_JSON_STATUS_INVALID;
  }
  parser->depth += 1U;
  focused_json_skip_whitespace(parser);
  if (focused_json_consume_byte(parser, '}')) {
    goto cleanup;
  }

  while (TRUE) {
    size_t key_length = 0U;
    gboolean is_name;
    gboolean is_focused;

    status = focused_json_parse_string_into(
        parser, parser->scratch, parser->scratch_capacity, &key_length, NULL);
    if (status != FOCUSED_JSON_STATUS_OK) {
      goto cleanup;
    }
    is_name = focused_bytes_equal(parser->scratch, key_length, "name");
    is_focused = focused_bytes_equal(parser->scratch, key_length, "focused");
    if ((is_name && name_seen) || (is_focused && focused_seen)) {
      status = FOCUSED_JSON_STATUS_INVALID;
      goto cleanup;
    }

    focused_json_skip_whitespace(parser);
    if (!focused_json_consume_byte(parser, ':')) {
      status = FOCUSED_JSON_STATUS_INVALID;
      goto cleanup;
    }
    focused_json_skip_whitespace(parser);

    if (is_name) {
      status = focused_json_parse_string_into(
          parser, parser->scratch, parser->scratch_capacity, &name_length,
          &name_raw);
      if (status != FOCUSED_JSON_STATUS_OK) {
        goto cleanup;
      }
      name_seen = TRUE;
    } else if (is_focused) {
      FocusedJsonValue value = {0};
      status = focused_json_parse_value(parser, &value);
      if (status != FOCUSED_JSON_STATUS_OK ||
          value.kind != FOCUSED_JSON_VALUE_BOOL) {
        status = FOCUSED_JSON_STATUS_INVALID;
        goto cleanup;
      }
      focused_seen = TRUE;
      focused = value.bool_value;
    } else {
      FocusedJsonValue value = {0};
      status = focused_json_parse_value(parser, &value);
      if (status != FOCUSED_JSON_STATUS_OK) {
        goto cleanup;
      }
    }

    focused_json_skip_whitespace(parser);
    if (focused_json_consume_byte(parser, '}')) {
      if (!name_seen) {
        status = FOCUSED_JSON_STATUS_INVALID;
        goto cleanup;
      }
      if (!*has_selected_name && focused && name_length != 0U) {
        *selected_name = name_raw;
        *has_selected_name = TRUE;
      }
      status = FOCUSED_JSON_STATUS_OK;
      goto cleanup;
    }
    if (!focused_json_consume_byte(parser, ',')) {
      status = FOCUSED_JSON_STATUS_INVALID;
      goto cleanup;
    }
    focused_json_skip_whitespace(parser);
  }

cleanup:
  parser->depth -= 1U;
  return status;
}

static FocusedJsonStatus focused_json_parse_sway_root(
    FocusedJsonParser *parser, FocusedJsonRawString *selected_name,
    gboolean *has_selected_name) {
  FocusedJsonStatus status = FOCUSED_JSON_STATUS_INVALID;

  *has_selected_name = FALSE;
  if (parser->depth >= 256U || !focused_json_consume_byte(parser, '[')) {
    return FOCUSED_JSON_STATUS_INVALID;
  }
  parser->depth += 1U;
  focused_json_skip_whitespace(parser);
  if (focused_json_consume_byte(parser, ']')) {
    status = FOCUSED_JSON_STATUS_OK;
    goto cleanup;
  }

  while (TRUE) {
    status = focused_json_parse_sway_item(parser, selected_name,
                                          has_selected_name);
    if (status != FOCUSED_JSON_STATUS_OK) {
      goto cleanup;
    }
    focused_json_skip_whitespace(parser);
    if (focused_json_consume_byte(parser, ']')) {
      status = FOCUSED_JSON_STATUS_OK;
      goto cleanup;
    }
    if (!focused_json_consume_byte(parser, ',')) {
      status = FOCUSED_JSON_STATUS_INVALID;
      goto cleanup;
    }
    focused_json_skip_whitespace(parser);
  }

cleanup:
  parser->depth -= 1U;
  return status;
}

static FocusedJsonStatus focused_json_parser_init(FocusedJsonParser *parser,
                                                  const uint8_t *data,
                                                  size_t length) {
  memset(parser, 0, sizeof(*parser));
  parser->data = data;
  parser->length = length;
  if (length == G_MAXSIZE) {
    return FOCUSED_JSON_STATUS_OUT_OF_MEMORY;
  }
  parser->scratch_capacity = length + 1U;
  parser->scratch = g_try_malloc(parser->scratch_capacity);
  if (parser->scratch == NULL) {
    memset(parser, 0, sizeof(*parser));
    return FOCUSED_JSON_STATUS_OUT_OF_MEMORY;
  }
  return FOCUSED_JSON_STATUS_OK;
}

static void focused_json_parser_clear(FocusedJsonParser *parser) {
  g_free(parser->scratch);
  memset(parser, 0, sizeof(*parser));
}

static ShaulaFocusedOutputStatus focused_output_copy_name(
    const uint8_t *data, size_t length, ShaulaFocusedOutputResult *result) {
  uint8_t *copy;

  if (length == 0U) {
    return SHAULA_FOCUSED_OUTPUT_STATUS_OK;
  }
  if (length == G_MAXSIZE) {
    return SHAULA_FOCUSED_OUTPUT_STATUS_OUT_OF_MEMORY;
  }
  copy = g_try_malloc(length + 1U);
  if (copy == NULL) {
    return SHAULA_FOCUSED_OUTPUT_STATUS_OUT_OF_MEMORY;
  }
  memcpy(copy, data, length);
  copy[length] = 0U;
  result->present = 1;
  result->name.data = copy;
  result->name.length = length;
  return SHAULA_FOCUSED_OUTPUT_STATUS_OK;
}

static ShaulaFocusedOutputStatus focused_output_decode_selected(
    FocusedJsonParser *parser, FocusedJsonRawString selected_name,
    ShaulaFocusedOutputResult *result) {
  size_t decoded_length = 0U;
  FocusedJsonStatus status;

  if (selected_name.end <= selected_name.start ||
      selected_name.end > parser->length) {
    return SHAULA_FOCUSED_OUTPUT_STATUS_OK;
  }
  parser->position = selected_name.start;
  status = focused_json_parse_string_into(
      parser, parser->scratch, parser->scratch_capacity, &decoded_length, NULL);
  if (status != FOCUSED_JSON_STATUS_OK ||
      parser->position != selected_name.end) {
    return SHAULA_FOCUSED_OUTPUT_STATUS_OK;
  }
  return focused_output_copy_name(parser->scratch, decoded_length, result);
}

typedef FocusedJsonStatus (*FocusedJsonRootParser)(
    FocusedJsonParser *parser, FocusedJsonRawString *selected_name,
    gboolean *has_selected_name);

static ShaulaFocusedOutputStatus focused_output_run_probe(
    const ShaulaProcessSpan *arguments, size_t argument_count,
    size_t stdout_limit, FocusedJsonRootParser parse_root,
    ShaulaFocusedOutputResult *result) {
  ShaulaProcessOutput output = {0};
  FocusedJsonParser parser;
  FocusedJsonRawString selected_name = {0};
  gboolean has_selected_name = FALSE;
  FocusedJsonStatus json_status;
  ShaulaFocusedOutputStatus final_status = SHAULA_FOCUSED_OUTPUT_STATUS_OK;
  const ShaulaProcessStatus process_status = shaula_process_run(
      (ShaulaProcessArgv){arguments, argument_count}, NULL, stdout_limit, 1024U,
      &output);

  if (process_status != SHAULA_PROCESS_STATUS_OK ||
      output.term_kind != SHAULA_PROCESS_TERM_EXITED || output.term_value != 0U) {
    shaula_process_output_clear(&output);
    return SHAULA_FOCUSED_OUTPUT_STATUS_OK;
  }

  json_status = focused_json_parser_init(
      &parser, (const uint8_t *)output.stdout_bytes.data,
      output.stdout_bytes.length);
  if (json_status != FOCUSED_JSON_STATUS_OK) {
    shaula_process_output_clear(&output);
    return SHAULA_FOCUSED_OUTPUT_STATUS_OK;
  }

  focused_json_skip_whitespace(&parser);
  json_status = parse_root(&parser, &selected_name, &has_selected_name);
  focused_json_skip_whitespace(&parser);
  if (json_status == FOCUSED_JSON_STATUS_OK &&
      parser.position == parser.length && has_selected_name) {
    final_status =
        focused_output_decode_selected(&parser, selected_name, result);
  }

  focused_json_parser_clear(&parser);
  shaula_process_output_clear(&output);
  return final_status;
}

static gboolean focused_span_ascii_equal_case_insensitive(ShaulaEnvSpan value,
                                                           const char *literal) {
  const size_t literal_length = strlen(literal);
  size_t index;

  if ((value.data == NULL && value.length != 0U) ||
      value.length != literal_length) {
    return FALSE;
  }
  for (index = 0U; index < value.length; index += 1U) {
    uint8_t left = (uint8_t)value.data[index];
    uint8_t right = (uint8_t)literal[index];
    if (left >= 'A' && left <= 'Z') {
      left = (uint8_t)(left + ('a' - 'A'));
    }
    if (right >= 'A' && right <= 'Z') {
      right = (uint8_t)(right + ('a' - 'A'));
    }
    if (left != right) {
      return FALSE;
    }
  }
  return TRUE;
}

void shaula_focused_output_result_init(ShaulaFocusedOutputResult *result) {
  if (result == NULL) {
    return;
  }
  result->present = 0;
  result->name.data = NULL;
  result->name.length = 0U;
}

void shaula_focused_output_result_clear(ShaulaFocusedOutputResult *result) {
  if (result == NULL) {
    return;
  }
  g_free(result->name.data);
  shaula_focused_output_result_init(result);
}

ShaulaFocusedOutputStatus shaula_focused_output_resolve(
    const ShaulaFocusedOutputEnvironment *environment,
    ShaulaFocusedOutputResult *out_result) {
  static const ShaulaProcessSpan niri_argv[] = {
      {"niri", sizeof("niri") - 1U},
      {"msg", sizeof("msg") - 1U},
      {"-j", sizeof("-j") - 1U},
      {"focused-output", sizeof("focused-output") - 1U},
  };
  static const ShaulaProcessSpan sway_argv[] = {
      {"swaymsg", sizeof("swaymsg") - 1U},
      {"-t", sizeof("-t") - 1U},
      {"get_outputs", sizeof("get_outputs") - 1U},
      {"-r", sizeof("-r") - 1U},
  };
  ShaulaEnvSpan override = {0};
  ShaulaCompositorDetection compositor = {0};

  if (out_result == NULL) {
    return SHAULA_FOCUSED_OUTPUT_STATUS_INVALID_ARGUMENT;
  }
  shaula_focused_output_result_clear(out_result);
  if (environment == NULL) {
    return SHAULA_FOCUSED_OUTPUT_STATUS_INVALID_ARGUMENT;
  }

  if (shaula_env_value_trimmed(environment->overlay_output_name, &override) ==
      SHAULA_ENV_STATUS_VALID) {
    return focused_output_copy_name((const uint8_t *)override.data,
                                    override.length, out_result);
  }

  if (shaula_compositor_detect(&environment->compositor, &compositor) !=
      SHAULA_COMPOSITOR_STATUS_OK) {
    return SHAULA_FOCUSED_OUTPUT_STATUS_OK;
  }
  if (compositor.kind == SHAULA_COMPOSITOR_KIND_NIRI) {
    return focused_output_run_probe(
        niri_argv, G_N_ELEMENTS(niri_argv), 8192U,
        focused_json_parse_niri_root, out_result);
  }
  if (focused_span_ascii_equal_case_insensitive(compositor.label, "sway")) {
    return focused_output_run_probe(
        sway_argv, G_N_ELEMENTS(sway_argv), 65536U,
        focused_json_parse_sway_root, out_result);
  }
  return SHAULA_FOCUSED_OUTPUT_STATUS_OK;
}
