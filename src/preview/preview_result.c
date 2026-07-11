#include "preview_result.h"

#include <glib.h>
#include <string.h>

typedef enum {
  PREVIEW_JSON_STATUS_OK,
  PREVIEW_JSON_STATUS_INVALID,
  PREVIEW_JSON_STATUS_OUT_OF_MEMORY,
} PreviewJsonStatus;

typedef enum {
  PREVIEW_JSON_VALUE_NULL,
  PREVIEW_JSON_VALUE_BOOL,
  PREVIEW_JSON_VALUE_STRING,
  PREVIEW_JSON_VALUE_OTHER,
} PreviewJsonValueKind;

typedef enum {
  PREVIEW_ROOT_FIELD_NONE,
  PREVIEW_ROOT_FIELD_CLOSED,
  PREVIEW_ROOT_FIELD_ACTION,
  PREVIEW_ROOT_FIELD_COPIED,
  PREVIEW_ROOT_FIELD_SAVED,
  PREVIEW_ROOT_FIELD_NOTIFIED,
  PREVIEW_ROOT_FIELD_SAVED_PATH,
} PreviewRootField;

typedef struct {
  size_t start;
  size_t end;
} PreviewJsonKeySpan;

typedef struct {
  const uint8_t *data;
  size_t length;
  size_t position;
  uint8_t *primary_scratch;
  uint8_t *secondary_scratch;
  size_t scratch_capacity;
  PreviewJsonKeySpan *keys;
  size_t key_count;
  size_t key_capacity;
} PreviewJsonParser;

typedef struct {
  PreviewJsonValueKind kind;
  gboolean bool_value;
  const uint8_t *string_data;
  size_t string_length;
} PreviewJsonValue;

static PreviewJsonStatus preview_json_parse_value(PreviewJsonParser *parser,
                                                  PreviewJsonValue *value);
static PreviewJsonStatus
preview_json_parse_object(PreviewJsonParser *parser,
                          ShaulaPreviewResult *root_output);
static PreviewJsonStatus preview_json_parse_array(PreviewJsonParser *parser);

static gboolean preview_span_equals(const uint8_t *data, size_t length,
                                    const char *literal) {
  const size_t literal_length = strlen(literal);
  return length == literal_length &&
         (length == 0 || memcmp(data, literal, length) == 0);
}

static gboolean preview_json_is_whitespace(uint8_t value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static void preview_json_skip_whitespace(PreviewJsonParser *parser) {
  while (parser->position < parser->length &&
         preview_json_is_whitespace(parser->data[parser->position])) {
    parser->position += 1;
  }
}

static gboolean preview_json_consume_byte(PreviewJsonParser *parser,
                                          uint8_t expected) {
  if (parser->position >= parser->length ||
      parser->data[parser->position] != expected) {
    return FALSE;
  }
  parser->position += 1;
  return TRUE;
}

static gboolean preview_json_consume_literal(PreviewJsonParser *parser,
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

static int preview_json_hex_value(uint8_t value) {
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

static gboolean preview_json_parse_hex_quad(PreviewJsonParser *parser,
                                            guint32 *value_out) {
  guint32 value = 0;
  if (parser->length - parser->position < 4) {
    return FALSE;
  }
  for (size_t index = 0; index < 4; index += 1) {
    const int digit = preview_json_hex_value(parser->data[parser->position]);
    if (digit < 0) {
      return FALSE;
    }
    value = (value << 4) | (guint32)digit;
    parser->position += 1;
  }
  *value_out = value;
  return TRUE;
}

static gboolean preview_json_append_bytes(uint8_t *output,
                                          size_t output_capacity,
                                          size_t *output_length,
                                          const uint8_t *data, size_t length) {
  if (length > output_capacity - *output_length) {
    return FALSE;
  }
  if (length != 0) {
    memcpy(output + *output_length, data, length);
  }
  *output_length += length;
  return TRUE;
}

static gboolean preview_json_append_codepoint(uint8_t *output,
                                              size_t output_capacity,
                                              size_t *output_length,
                                              gunichar codepoint) {
  gchar encoded[6];
  if (!g_unichar_validate(codepoint)) {
    return FALSE;
  }
  const gint encoded_length = g_unichar_to_utf8(codepoint, encoded);
  if (encoded_length <= 0) {
    return FALSE;
  }
  return preview_json_append_bytes(output, output_capacity, output_length,
                                   (const uint8_t *)encoded,
                                   (size_t)encoded_length);
}

static size_t preview_json_utf8_sequence_length(uint8_t first) {
  if (first >= 0xc2 && first <= 0xdf) {
    return 2;
  }
  if (first >= 0xe0 && first <= 0xef) {
    return 3;
  }
  if (first >= 0xf0 && first <= 0xf4) {
    return 4;
  }
  return 0;
}

static gboolean preview_json_append_raw_utf8(PreviewJsonParser *parser,
                                             uint8_t *output,
                                             size_t output_capacity,
                                             size_t *output_length) {
  const size_t sequence_length =
      preview_json_utf8_sequence_length(parser->data[parser->position]);
  if (sequence_length == 0 ||
      sequence_length > parser->length - parser->position) {
    return FALSE;
  }

  const gchar *sequence = (const gchar *)(parser->data + parser->position);
  const gunichar codepoint =
      g_utf8_get_char_validated(sequence, (gssize)sequence_length);
  if (codepoint == (gunichar)-1 || codepoint == (gunichar)-2 ||
      !g_unichar_validate(codepoint)) {
    return FALSE;
  }
  if (!preview_json_append_bytes(output, output_capacity, output_length,
                                 parser->data + parser->position,
                                 sequence_length)) {
    return FALSE;
  }
  parser->position += sequence_length;
  return TRUE;
}

static gboolean preview_json_append_escape(PreviewJsonParser *parser,
                                           uint8_t *output,
                                           size_t output_capacity,
                                           size_t *output_length) {
  if (parser->position >= parser->length) {
    return FALSE;
  }

  const uint8_t escaped = parser->data[parser->position];
  parser->position += 1;
  switch (escaped) {
  case '"':
  case '\\':
  case '/':
    return preview_json_append_bytes(output, output_capacity, output_length,
                                     &escaped, 1);
  case 'b': {
    const uint8_t value = '\b';
    return preview_json_append_bytes(output, output_capacity, output_length,
                                     &value, 1);
  }
  case 'f': {
    const uint8_t value = '\f';
    return preview_json_append_bytes(output, output_capacity, output_length,
                                     &value, 1);
  }
  case 'n': {
    const uint8_t value = '\n';
    return preview_json_append_bytes(output, output_capacity, output_length,
                                     &value, 1);
  }
  case 'r': {
    const uint8_t value = '\r';
    return preview_json_append_bytes(output, output_capacity, output_length,
                                     &value, 1);
  }
  case 't': {
    const uint8_t value = '\t';
    return preview_json_append_bytes(output, output_capacity, output_length,
                                     &value, 1);
  }
  case 'u': {
    guint32 first = 0;
    if (!preview_json_parse_hex_quad(parser, &first)) {
      return FALSE;
    }

    gunichar codepoint = (gunichar)first;
    if (first >= 0xd800 && first <= 0xdbff) {
      if (parser->length - parser->position < 6 ||
          parser->data[parser->position] != '\\' ||
          parser->data[parser->position + 1] != 'u') {
        return FALSE;
      }
      parser->position += 2;
      guint32 second = 0;
      if (!preview_json_parse_hex_quad(parser, &second) || second < 0xdc00 ||
          second > 0xdfff) {
        return FALSE;
      }
      codepoint =
          (gunichar)(0x10000 + ((first - 0xd800) << 10) + (second - 0xdc00));
    } else if (first >= 0xdc00 && first <= 0xdfff) {
      return FALSE;
    }

    return preview_json_append_codepoint(output, output_capacity, output_length,
                                         codepoint);
  }
  default:
    return FALSE;
  }
}

static PreviewJsonStatus
preview_json_parse_string_into(PreviewJsonParser *parser, uint8_t *output,
                               size_t output_capacity, size_t *output_length,
                               size_t *raw_start, size_t *raw_end) {
  if (!preview_json_consume_byte(parser, '"')) {
    return PREVIEW_JSON_STATUS_INVALID;
  }
  if (raw_start != NULL) {
    *raw_start = parser->position - 1;
  }

  size_t length = 0;
  while (parser->position < parser->length) {
    const uint8_t current = parser->data[parser->position];
    if (current == '"') {
      parser->position += 1;
      if (length >= output_capacity) {
        return PREVIEW_JSON_STATUS_INVALID;
      }
      output[length] = 0;
      *output_length = length;
      if (raw_end != NULL) {
        *raw_end = parser->position;
      }
      return PREVIEW_JSON_STATUS_OK;
    }
    if (current < 0x20) {
      return PREVIEW_JSON_STATUS_INVALID;
    }
    if (current == '\\') {
      parser->position += 1;
      if (!preview_json_append_escape(parser, output, output_capacity, &length)) {
        return PREVIEW_JSON_STATUS_INVALID;
      }
      continue;
    }
    if (current < 0x80) {
      if (!preview_json_append_bytes(output, output_capacity, &length, &current,
                                     1)) {
        return PREVIEW_JSON_STATUS_INVALID;
      }
      parser->position += 1;
      continue;
    }
    if (!preview_json_append_raw_utf8(parser, output, output_capacity, &length)) {
      return PREVIEW_JSON_STATUS_INVALID;
    }
  }
  return PREVIEW_JSON_STATUS_INVALID;
}

static gboolean preview_json_parse_number(PreviewJsonParser *parser) {
  size_t position = parser->position;
  if (position < parser->length && parser->data[position] == '-') {
    position += 1;
  }
  if (position >= parser->length) {
    return FALSE;
  }

  if (parser->data[position] == '0') {
    position += 1;
  } else if (parser->data[position] >= '1' &&
             parser->data[position] <= '9') {
    position += 1;
    while (position < parser->length && parser->data[position] >= '0' &&
           parser->data[position] <= '9') {
      position += 1;
    }
  } else {
    return FALSE;
  }

  if (position < parser->length && parser->data[position] == '.') {
    position += 1;
    if (position >= parser->length || parser->data[position] < '0' ||
        parser->data[position] > '9') {
      return FALSE;
    }
    while (position < parser->length && parser->data[position] >= '0' &&
           parser->data[position] <= '9') {
      position += 1;
    }
  }

  if (position < parser->length &&
      (parser->data[position] == 'e' || parser->data[position] == 'E')) {
    position += 1;
    if (position < parser->length &&
        (parser->data[position] == '+' || parser->data[position] == '-')) {
      position += 1;
    }
    if (position >= parser->length || parser->data[position] < '0' ||
        parser->data[position] > '9') {
      return FALSE;
    }
    while (position < parser->length && parser->data[position] >= '0' &&
           parser->data[position] <= '9') {
      position += 1;
    }
  }

  parser->position = position;
  return TRUE;
}

static PreviewRootField preview_root_field_from_bytes(const uint8_t *data,
                                                       size_t length) {
  if (preview_span_equals(data, length, "closed")) {
    return PREVIEW_ROOT_FIELD_CLOSED;
  }
  if (preview_span_equals(data, length, "action")) {
    return PREVIEW_ROOT_FIELD_ACTION;
  }
  if (preview_span_equals(data, length, "copied")) {
    return PREVIEW_ROOT_FIELD_COPIED;
  }
  if (preview_span_equals(data, length, "saved")) {
    return PREVIEW_ROOT_FIELD_SAVED;
  }
  if (preview_span_equals(data, length, "notified")) {
    return PREVIEW_ROOT_FIELD_NOTIFIED;
  }
  if (preview_span_equals(data, length, "saved_path")) {
    return PREVIEW_ROOT_FIELD_SAVED_PATH;
  }
  return PREVIEW_ROOT_FIELD_NONE;
}

static ShaulaPreviewAction preview_action_from_bytes(const uint8_t *data,
                                                     size_t length) {
  if (preview_span_equals(data, length, "close")) {
    return SHAULA_PREVIEW_ACTION_CLOSE;
  }
  if (preview_span_equals(data, length, "copy")) {
    return SHAULA_PREVIEW_ACTION_COPY;
  }
  if (preview_span_equals(data, length, "save")) {
    return SHAULA_PREVIEW_ACTION_SAVE;
  }
  if (preview_span_equals(data, length, "discard")) {
    return SHAULA_PREVIEW_ACTION_DISCARD;
  }
  return SHAULA_PREVIEW_ACTION_UNKNOWN;
}

static PreviewJsonStatus
preview_json_copy_saved_path(const uint8_t *data, size_t length,
                             ShaulaPreviewResult *output) {
  if (length == 0) {
    return PREVIEW_JSON_STATUS_OK;
  }
  if (length == G_MAXSIZE) {
    return PREVIEW_JSON_STATUS_OUT_OF_MEMORY;
  }

  uint8_t *copy = g_try_malloc(length + 1);
  if (copy == NULL) {
    return PREVIEW_JSON_STATUS_OUT_OF_MEMORY;
  }
  memcpy(copy, data, length);
  copy[length] = 0;
  output->saved_path.data = copy;
  output->saved_path.length = length;
  return PREVIEW_JSON_STATUS_OK;
}

static PreviewJsonStatus
preview_json_apply_root_field(PreviewRootField field,
                              const PreviewJsonValue *value,
                              ShaulaPreviewResult *output) {
  switch (field) {
  case PREVIEW_ROOT_FIELD_CLOSED:
    if (value->kind == PREVIEW_JSON_VALUE_BOOL) {
      output->closed = value->bool_value ? 1 : 0;
    }
    return PREVIEW_JSON_STATUS_OK;
  case PREVIEW_ROOT_FIELD_ACTION:
    if (value->kind == PREVIEW_JSON_VALUE_STRING) {
      output->action =
          preview_action_from_bytes(value->string_data, value->string_length);
    }
    return PREVIEW_JSON_STATUS_OK;
  case PREVIEW_ROOT_FIELD_COPIED:
    if (value->kind == PREVIEW_JSON_VALUE_BOOL) {
      output->copied = value->bool_value ? 1 : 0;
    }
    return PREVIEW_JSON_STATUS_OK;
  case PREVIEW_ROOT_FIELD_SAVED:
    if (value->kind == PREVIEW_JSON_VALUE_BOOL) {
      output->saved = value->bool_value ? 1 : 0;
    }
    return PREVIEW_JSON_STATUS_OK;
  case PREVIEW_ROOT_FIELD_NOTIFIED:
    if (value->kind == PREVIEW_JSON_VALUE_BOOL) {
      output->notified = value->bool_value ? 1 : 0;
    }
    return PREVIEW_JSON_STATUS_OK;
  case PREVIEW_ROOT_FIELD_SAVED_PATH:
    if (value->kind == PREVIEW_JSON_VALUE_STRING) {
      return preview_json_copy_saved_path(value->string_data,
                                          value->string_length, output);
    }
    return PREVIEW_JSON_STATUS_OK;
  case PREVIEW_ROOT_FIELD_NONE:
    return PREVIEW_JSON_STATUS_OK;
  }
  return PREVIEW_JSON_STATUS_INVALID;
}

static PreviewJsonStatus
preview_json_key_is_duplicate(PreviewJsonParser *parser, size_t scope_start,
                              size_t current_length,
                              gboolean *is_duplicate) {
  *is_duplicate = FALSE;
  for (size_t index = scope_start; index < parser->key_count; index += 1) {
    PreviewJsonParser previous = *parser;
    previous.position = parser->keys[index].start;
    previous.length = parser->keys[index].end;
    size_t previous_length = 0;
    const PreviewJsonStatus status = preview_json_parse_string_into(
        &previous, parser->secondary_scratch, parser->scratch_capacity,
        &previous_length, NULL, NULL);
    if (status != PREVIEW_JSON_STATUS_OK ||
        previous.position != parser->keys[index].end) {
      return PREVIEW_JSON_STATUS_INVALID;
    }
    if (previous_length == current_length &&
        (current_length == 0 ||
         memcmp(parser->secondary_scratch, parser->primary_scratch,
                current_length) == 0)) {
      *is_duplicate = TRUE;
      return PREVIEW_JSON_STATUS_OK;
    }
  }
  return PREVIEW_JSON_STATUS_OK;
}

static PreviewJsonStatus preview_json_parse_array(PreviewJsonParser *parser) {
  if (!preview_json_consume_byte(parser, '[')) {
    return PREVIEW_JSON_STATUS_INVALID;
  }
  preview_json_skip_whitespace(parser);
  if (preview_json_consume_byte(parser, ']')) {
    return PREVIEW_JSON_STATUS_OK;
  }

  while (TRUE) {
    PreviewJsonValue value = {0};
    const PreviewJsonStatus status = preview_json_parse_value(parser, &value);
    if (status != PREVIEW_JSON_STATUS_OK) {
      return status;
    }
    preview_json_skip_whitespace(parser);
    if (preview_json_consume_byte(parser, ']')) {
      return PREVIEW_JSON_STATUS_OK;
    }
    if (!preview_json_consume_byte(parser, ',')) {
      return PREVIEW_JSON_STATUS_INVALID;
    }
    preview_json_skip_whitespace(parser);
  }
}

static PreviewJsonStatus
preview_json_parse_object(PreviewJsonParser *parser,
                          ShaulaPreviewResult *root_output) {
  if (!preview_json_consume_byte(parser, '{')) {
    return PREVIEW_JSON_STATUS_INVALID;
  }

  const size_t scope_start = parser->key_count;
  PreviewJsonStatus final_status = PREVIEW_JSON_STATUS_INVALID;
  preview_json_skip_whitespace(parser);
  if (preview_json_consume_byte(parser, '}')) {
    final_status = PREVIEW_JSON_STATUS_OK;
    goto cleanup;
  }

  while (TRUE) {
    size_t key_length = 0;
    size_t key_start = 0;
    size_t key_end = 0;
    PreviewJsonStatus status = preview_json_parse_string_into(
        parser, parser->primary_scratch, parser->scratch_capacity, &key_length,
        &key_start, &key_end);
    if (status != PREVIEW_JSON_STATUS_OK) {
      final_status = status;
      goto cleanup;
    }

    gboolean duplicate = FALSE;
    status = preview_json_key_is_duplicate(parser, scope_start, key_length,
                                           &duplicate);
    if (status != PREVIEW_JSON_STATUS_OK || duplicate) {
      final_status = status == PREVIEW_JSON_STATUS_OK
                         ? PREVIEW_JSON_STATUS_INVALID
                         : status;
      goto cleanup;
    }
    if (parser->key_count >= parser->key_capacity) {
      final_status = PREVIEW_JSON_STATUS_OUT_OF_MEMORY;
      goto cleanup;
    }
    parser->keys[parser->key_count] =
        (PreviewJsonKeySpan){.start = key_start, .end = key_end};
    parser->key_count += 1;

    const PreviewRootField field =
        root_output != NULL
            ? preview_root_field_from_bytes(parser->primary_scratch, key_length)
            : PREVIEW_ROOT_FIELD_NONE;

    preview_json_skip_whitespace(parser);
    if (!preview_json_consume_byte(parser, ':')) {
      final_status = PREVIEW_JSON_STATUS_INVALID;
      goto cleanup;
    }
    preview_json_skip_whitespace(parser);

    PreviewJsonValue value = {0};
    status = preview_json_parse_value(parser, &value);
    if (status != PREVIEW_JSON_STATUS_OK) {
      final_status = status;
      goto cleanup;
    }
    if (root_output != NULL) {
      status = preview_json_apply_root_field(field, &value, root_output);
      if (status != PREVIEW_JSON_STATUS_OK) {
        final_status = status;
        goto cleanup;
      }
    }

    preview_json_skip_whitespace(parser);
    if (preview_json_consume_byte(parser, '}')) {
      final_status = PREVIEW_JSON_STATUS_OK;
      goto cleanup;
    }
    if (!preview_json_consume_byte(parser, ',')) {
      final_status = PREVIEW_JSON_STATUS_INVALID;
      goto cleanup;
    }
    preview_json_skip_whitespace(parser);
  }

cleanup:
  parser->key_count = scope_start;
  return final_status;
}

static PreviewJsonStatus preview_json_parse_value(PreviewJsonParser *parser,
                                                  PreviewJsonValue *value) {
  value->kind = PREVIEW_JSON_VALUE_OTHER;
  value->bool_value = FALSE;
  value->string_data = NULL;
  value->string_length = 0;

  preview_json_skip_whitespace(parser);
  if (parser->position >= parser->length) {
    return PREVIEW_JSON_STATUS_INVALID;
  }

  switch (parser->data[parser->position]) {
  case 'n':
    if (!preview_json_consume_literal(parser, "null")) {
      return PREVIEW_JSON_STATUS_INVALID;
    }
    value->kind = PREVIEW_JSON_VALUE_NULL;
    return PREVIEW_JSON_STATUS_OK;
  case 't':
    if (!preview_json_consume_literal(parser, "true")) {
      return PREVIEW_JSON_STATUS_INVALID;
    }
    value->kind = PREVIEW_JSON_VALUE_BOOL;
    value->bool_value = TRUE;
    return PREVIEW_JSON_STATUS_OK;
  case 'f':
    if (!preview_json_consume_literal(parser, "false")) {
      return PREVIEW_JSON_STATUS_INVALID;
    }
    value->kind = PREVIEW_JSON_VALUE_BOOL;
    value->bool_value = FALSE;
    return PREVIEW_JSON_STATUS_OK;
  case '"': {
    size_t length = 0;
    const PreviewJsonStatus status = preview_json_parse_string_into(
        parser, parser->primary_scratch, parser->scratch_capacity, &length, NULL,
        NULL);
    if (status != PREVIEW_JSON_STATUS_OK) {
      return status;
    }
    value->kind = PREVIEW_JSON_VALUE_STRING;
    value->string_data = parser->primary_scratch;
    value->string_length = length;
    return PREVIEW_JSON_STATUS_OK;
  }
  case '{':
    return preview_json_parse_object(parser, NULL);
  case '[':
    return preview_json_parse_array(parser);
  default:
    if (!preview_json_parse_number(parser)) {
      return PREVIEW_JSON_STATUS_INVALID;
    }
    return PREVIEW_JSON_STATUS_OK;
  }
}

static PreviewJsonStatus preview_json_parser_init(PreviewJsonParser *parser,
                                                  ShaulaPreviewResultSpan input) {
  memset(parser, 0, sizeof(*parser));
  parser->data = input.data;
  parser->length = input.length;
  if (input.length == G_MAXSIZE) {
    return PREVIEW_JSON_STATUS_OUT_OF_MEMORY;
  }

  parser->scratch_capacity = input.length + 1;
  parser->primary_scratch = g_try_malloc(parser->scratch_capacity);
  parser->secondary_scratch = g_try_malloc(parser->scratch_capacity);
  parser->key_capacity = (input.length / 2) + 1;
  parser->keys = g_try_new(PreviewJsonKeySpan, parser->key_capacity);
  if (parser->primary_scratch == NULL || parser->secondary_scratch == NULL ||
      parser->keys == NULL) {
    g_free(parser->primary_scratch);
    g_free(parser->secondary_scratch);
    g_free(parser->keys);
    memset(parser, 0, sizeof(*parser));
    return PREVIEW_JSON_STATUS_OUT_OF_MEMORY;
  }
  return PREVIEW_JSON_STATUS_OK;
}

static void preview_json_parser_clear(PreviewJsonParser *parser) {
  g_free(parser->primary_scratch);
  g_free(parser->secondary_scratch);
  g_free(parser->keys);
  memset(parser, 0, sizeof(*parser));
}

void shaula_preview_result_init(ShaulaPreviewResult *result) {
  if (result == NULL) {
    return;
  }
  result->closed = 0;
  result->action = SHAULA_PREVIEW_ACTION_UNKNOWN;
  result->copied = 0;
  result->saved = 0;
  result->notified = 0;
  result->saved_path.data = NULL;
  result->saved_path.length = 0;
}

void shaula_preview_result_clear(ShaulaPreviewResult *result) {
  if (result == NULL) {
    return;
  }
  g_free(result->saved_path.data);
  shaula_preview_result_init(result);
}

ShaulaPreviewResultStatus
shaula_preview_result_parse(ShaulaPreviewResultSpan input,
                            ShaulaPreviewResult *output) {
  if (output == NULL) {
    return SHAULA_PREVIEW_RESULT_STATUS_INVALID_ARGUMENT;
  }

  shaula_preview_result_clear(output);
  if (input.data == NULL && input.length != 0) {
    return SHAULA_PREVIEW_RESULT_STATUS_INVALID_ARGUMENT;
  }
  if (input.length == G_MAXSIZE) {
    return SHAULA_PREVIEW_RESULT_STATUS_OUT_OF_MEMORY;
  }

  size_t start = 0;
  size_t end = input.length;
  while (start < end && preview_json_is_whitespace(input.data[start])) {
    start += 1;
  }
  while (end > start && preview_json_is_whitespace(input.data[end - 1])) {
    end -= 1;
  }
  if (start == end) {
    return SHAULA_PREVIEW_RESULT_STATUS_MISSING;
  }

  const ShaulaPreviewResultSpan trimmed = {
      .data = input.data + start,
      .length = end - start,
  };
  PreviewJsonParser parser;
  PreviewJsonStatus status = preview_json_parser_init(&parser, trimmed);
  if (status != PREVIEW_JSON_STATUS_OK) {
    return SHAULA_PREVIEW_RESULT_STATUS_OUT_OF_MEMORY;
  }

  if (parser.data[0] != '{') {
    status = PREVIEW_JSON_STATUS_INVALID;
  } else {
    status = preview_json_parse_object(&parser, output);
    if (status == PREVIEW_JSON_STATUS_OK && parser.position != parser.length) {
      status = PREVIEW_JSON_STATUS_INVALID;
    }
  }
  preview_json_parser_clear(&parser);

  if (status == PREVIEW_JSON_STATUS_OK) {
    return SHAULA_PREVIEW_RESULT_STATUS_OK;
  }
  shaula_preview_result_clear(output);
  if (status == PREVIEW_JSON_STATUS_OUT_OF_MEMORY) {
    return SHAULA_PREVIEW_RESULT_STATUS_OUT_OF_MEMORY;
  }
  return SHAULA_PREVIEW_RESULT_STATUS_INVALID_JSON;
}

ShaulaPreviewResultSpan
shaula_preview_action_token(ShaulaPreviewAction action) {
  static const uint8_t close_token[] = "close";
  static const uint8_t copy_token[] = "copy";
  static const uint8_t save_token[] = "save";
  static const uint8_t discard_token[] = "discard";
  static const uint8_t unknown_token[] = "unknown";

  switch (action) {
  case SHAULA_PREVIEW_ACTION_CLOSE:
    return (ShaulaPreviewResultSpan){close_token, sizeof(close_token) - 1};
  case SHAULA_PREVIEW_ACTION_COPY:
    return (ShaulaPreviewResultSpan){copy_token, sizeof(copy_token) - 1};
  case SHAULA_PREVIEW_ACTION_SAVE:
    return (ShaulaPreviewResultSpan){save_token, sizeof(save_token) - 1};
  case SHAULA_PREVIEW_ACTION_DISCARD:
    return (ShaulaPreviewResultSpan){discard_token,
                                     sizeof(discard_token) - 1};
  case SHAULA_PREVIEW_ACTION_UNKNOWN:
    return (ShaulaPreviewResultSpan){unknown_token,
                                     sizeof(unknown_token) - 1};
  default:
    return (ShaulaPreviewResultSpan){NULL, 0};
  }
}
