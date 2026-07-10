#include "env.h"

#include <string.h>

static void clear_span(ShaulaEnvSpan *span) {
  if (span == NULL) {
    return;
  }
  span->data = NULL;
  span->length = 0;
}

static int is_trim_byte(unsigned char value) {
  return value == (unsigned char)' ' || value == (unsigned char)'\t' ||
         value == (unsigned char)'\r' || value == (unsigned char)'\n';
}

static ShaulaEnvSpan trim_span(ShaulaEnvSpan value) {
  size_t start = 0;
  size_t end = value.length;

  while (start < end && is_trim_byte((unsigned char)value.data[start])) {
    start += 1;
  }
  while (end > start && is_trim_byte((unsigned char)value.data[end - 1])) {
    end -= 1;
  }

  return (ShaulaEnvSpan){value.data + start, end - start};
}

static unsigned char ascii_lower(unsigned char value) {
  if (value >= (unsigned char)'A' && value <= (unsigned char)'Z') {
    return (unsigned char)(value + ((unsigned char)'a' - (unsigned char)'A'));
  }
  return value;
}

static int span_equals_ascii_case_insensitive(ShaulaEnvSpan value,
                                              const char *expected) {
  size_t expected_length = strlen(expected);
  size_t index;

  if (value.length != expected_length) {
    return 0;
  }

  for (index = 0; index < value.length; index += 1) {
    if (ascii_lower((unsigned char)value.data[index]) !=
        ascii_lower((unsigned char)expected[index])) {
      return 0;
    }
  }

  return 1;
}

ShaulaEnvStatus shaula_env_value_slice(const char *value, ShaulaEnvSpan *out) {
  if (out == NULL) {
    return SHAULA_ENV_STATUS_INVALID;
  }

  clear_span(out);
  if (value == NULL) {
    return SHAULA_ENV_STATUS_MISSING;
  }

  out->data = value;
  out->length = strlen(value);
  return SHAULA_ENV_STATUS_VALID;
}

ShaulaEnvStatus shaula_env_value_trimmed(const char *value,
                                         ShaulaEnvSpan *out) {
  ShaulaEnvSpan raw;
  ShaulaEnvSpan trimmed;
  ShaulaEnvStatus status;

  if (out == NULL) {
    return SHAULA_ENV_STATUS_INVALID;
  }

  clear_span(out);
  status = shaula_env_value_slice(value, &raw);
  if (status != SHAULA_ENV_STATUS_VALID) {
    return status;
  }

  trimmed = trim_span(raw);
  if (trimmed.length == 0) {
    return SHAULA_ENV_STATUS_MISSING;
  }

  *out = trimmed;
  return SHAULA_ENV_STATUS_VALID;
}

ShaulaEnvStatus shaula_env_value_flag(const char *value, int32_t *out_value) {
  ShaulaEnvSpan trimmed;
  ShaulaEnvStatus status;

  if (out_value == NULL) {
    return SHAULA_ENV_STATUS_INVALID;
  }

  *out_value = 0;
  status = shaula_env_value_trimmed(value, &trimmed);
  if (status != SHAULA_ENV_STATUS_VALID) {
    return status;
  }

  if ((trimmed.length == 1 && trimmed.data[0] == '1') ||
      span_equals_ascii_case_insensitive(trimmed, "true") ||
      span_equals_ascii_case_insensitive(trimmed, "yes")) {
    *out_value = 1;
    return SHAULA_ENV_STATUS_VALID;
  }

  if ((trimmed.length == 1 && trimmed.data[0] == '0') ||
      span_equals_ascii_case_insensitive(trimmed, "false") ||
      span_equals_ascii_case_insensitive(trimmed, "no")) {
    return SHAULA_ENV_STATUS_VALID;
  }

  return SHAULA_ENV_STATUS_INVALID;
}

uint64_t shaula_env_value_unsigned_or_default(const char *value,
                                              uint64_t max_value,
                                              uint64_t default_value) {
  ShaulaEnvSpan trimmed;
  ShaulaEnvStatus status;
  size_t index = 0;
  int negative = 0;
  uint64_t parsed = 0;

  status = shaula_env_value_trimmed(value, &trimmed);
  if (status != SHAULA_ENV_STATUS_VALID) {
    return default_value;
  }

  if (trimmed.data[index] == '+' || trimmed.data[index] == '-') {
    negative = trimmed.data[index] == '-';
    index += 1;
    if (index == trimmed.length) {
      return default_value;
    }
  }

  if (trimmed.data[index] == '_' || trimmed.data[trimmed.length - 1] == '_') {
    return default_value;
  }

  for (; index < trimmed.length; index += 1) {
    unsigned char byte = (unsigned char)trimmed.data[index];
    uint64_t digit;

    if (byte == (unsigned char)'_') {
      continue;
    }
    if (byte < (unsigned char)'0' || byte > (unsigned char)'9') {
      return default_value;
    }

    digit = (uint64_t)(byte - (unsigned char)'0');
    if (digit > max_value || parsed > (max_value - digit) / UINT64_C(10)) {
      return default_value;
    }
    parsed = parsed * UINT64_C(10) + digit;
  }

  if (negative && parsed != 0) {
    return default_value;
  }

  return parsed;
}

ShaulaEnvStatus shaula_env_first_desktop_token(ShaulaEnvSpan value,
                                               ShaulaEnvSpan *out) {
  size_t token_start = 0;
  size_t index;

  if (out == NULL) {
    return SHAULA_ENV_STATUS_INVALID;
  }

  clear_span(out);
  if (value.data == NULL) {
    return SHAULA_ENV_STATUS_MISSING;
  }

  for (index = 0; index <= value.length; index += 1) {
    int at_end = index == value.length;
    int at_separator =
        !at_end && (value.data[index] == ':' || value.data[index] == ';');

    if (at_end || at_separator) {
      ShaulaEnvSpan token = trim_span(
          (ShaulaEnvSpan){value.data + token_start, index - token_start});
      if (token.length > 0) {
        *out = token;
        return SHAULA_ENV_STATUS_VALID;
      }
      token_start = index + 1;
    }
  }

  return SHAULA_ENV_STATUS_MISSING;
}
