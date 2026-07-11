#include "cli/json.h"

#include <glib.h>
#include <limits.h>
#include <string.h>

static const uint8_t contract_version[] = "1.0.0";

typedef struct {
    uint8_t *data;
    size_t length;
    size_t capacity;
} JsonBuilder;

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

static ShaulaJsonStatus builder_reserve(JsonBuilder *builder, size_t additional) {
    size_t required_without_nul = 0U;
    size_t required = 0U;
    size_t new_capacity = 0U;
    uint8_t *new_data = NULL;

    if (!checked_add_size(builder->length, additional, &required_without_nul) ||
        !checked_add_size(required_without_nul, 1U, &required)) {
        return SHAULA_JSON_STATUS_SIZE_OVERFLOW;
    }
    if (required <= builder->capacity) {
        return SHAULA_JSON_STATUS_OK;
    }

    new_capacity = builder->capacity == 0U ? 64U : builder->capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2U) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2U;
    }

    new_data = g_try_realloc(builder->data, new_capacity);
    if (new_data == NULL) {
        return SHAULA_JSON_STATUS_OUT_OF_MEMORY;
    }
    builder->data = new_data;
    builder->capacity = new_capacity;
    return SHAULA_JSON_STATUS_OK;
}

static ShaulaJsonStatus builder_append(JsonBuilder *builder, const uint8_t *data, size_t length) {
    ShaulaJsonStatus status = SHAULA_JSON_STATUS_OK;

    if (data == NULL && length != 0U) {
        return SHAULA_JSON_STATUS_INVALID_ARGUMENT;
    }
    status = builder_reserve(builder, length);
    if (status != SHAULA_JSON_STATUS_OK) {
        return status;
    }
    if (length != 0U) {
        memcpy(builder->data + builder->length, data, length);
        builder->length += length;
    }
    return SHAULA_JSON_STATUS_OK;
}

static ShaulaJsonStatus builder_append_literal(JsonBuilder *builder, const char *literal) {
    return builder_append(builder, (const uint8_t *)literal, strlen(literal));
}

static ShaulaJsonStatus builder_append_byte(JsonBuilder *builder, uint8_t value) {
    return builder_append(builder, &value, 1U);
}

static void builder_clear(JsonBuilder *builder) {
    g_free(builder->data);
    builder->data = NULL;
    builder->length = 0U;
    builder->capacity = 0U;
}

static ShaulaJsonStatus builder_finish(JsonBuilder *builder, ShaulaJsonOwnedBytes *output) {
    ShaulaJsonStatus status = builder_reserve(builder, 0U);

    if (status != SHAULA_JSON_STATUS_OK) {
        return status;
    }
    builder->data[builder->length] = 0U;
    output->data = builder->data;
    output->length = builder->length;
    builder->data = NULL;
    builder->length = 0U;
    builder->capacity = 0U;
    return SHAULA_JSON_STATUS_OK;
}

static ShaulaJsonStatus append_control_escape(JsonBuilder *builder, uint8_t value) {
    static const uint8_t hex[] = "0123456789abcdef";
    uint8_t unicode_escape[6] = {'\\', 'u', '0', '0', 0U, 0U};

    switch (value) {
        case '\\':
            return builder_append_literal(builder, "\\\\");
        case '"':
            return builder_append_literal(builder, "\\\"");
        case 0x08:
            return builder_append_literal(builder, "\\b");
        case 0x0c:
            return builder_append_literal(builder, "\\f");
        case '\n':
            return builder_append_literal(builder, "\\n");
        case '\r':
            return builder_append_literal(builder, "\\r");
        case '\t':
            return builder_append_literal(builder, "\\t");
        default:
            unicode_escape[4] = hex[(value >> 4U) & 0x0fU];
            unicode_escape[5] = hex[value & 0x0fU];
            return builder_append(builder, unicode_escape, sizeof(unicode_escape));
    }
}

static ShaulaJsonStatus builder_append_escaped_string(JsonBuilder *builder, ShaulaJsonSpan value) {
    ShaulaJsonStatus status = SHAULA_JSON_STATUS_OK;
    size_t index = 0U;

    if (!span_is_valid(value)) {
        return SHAULA_JSON_STATUS_INVALID_ARGUMENT;
    }
    if (value.length > (SIZE_MAX - 3U) / 6U) {
        return SHAULA_JSON_STATUS_SIZE_OVERFLOW;
    }

    status = builder_append_byte(builder, '"');
    if (status != SHAULA_JSON_STATUS_OK) {
        return status;
    }
    for (index = 0U; index < value.length; index += 1U) {
        const uint8_t byte = value.data[index];
        if (byte < 0x20U || byte == '\\' || byte == '"') {
            status = append_control_escape(builder, byte);
        } else {
            status = builder_append_byte(builder, byte);
        }
        if (status != SHAULA_JSON_STATUS_OK) {
            return status;
        }
    }
    return builder_append_byte(builder, '"');
}

static int64_t floor_divide(int64_t dividend, int64_t divisor) {
    int64_t quotient = dividend / divisor;
    const int64_t remainder = dividend % divisor;

    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        quotient -= 1;
    }
    return quotient;
}

static int64_t floor_modulo(int64_t dividend, int64_t divisor) {
    return dividend - floor_divide(dividend, divisor) * divisor;
}

static ShaulaJsonStatus format_timestamp(
    int64_t epoch_seconds,
    uint8_t output[20]) {
    const int64_t days = floor_divide(epoch_seconds, 86400);
    const int64_t seconds_of_day = floor_modulo(epoch_seconds, 86400);
    const int64_t z = days + 719468;
    const int64_t adjusted_z = z >= 0 ? z : z - 146096;
    const int64_t era = floor_divide(adjusted_z, 146097);
    const int64_t day_of_era = z - era * 146097;
    const int64_t year_of_era = floor_divide(
        day_of_era - floor_divide(day_of_era, 1460) +
            floor_divide(day_of_era, 36524) - floor_divide(day_of_era, 146096),
        365);
    int64_t year = year_of_era + era * 400;
    const int64_t day_of_year = day_of_era -
        (365 * year_of_era + floor_divide(year_of_era, 4) -
         floor_divide(year_of_era, 100));
    const int64_t month_part = floor_divide(5 * day_of_year + 2, 153);
    const int64_t day = day_of_year - floor_divide(153 * month_part + 2, 5) + 1;
    int64_t month = month_part + (month_part < 10 ? 3 : -9);
    const int64_t hour = floor_divide(seconds_of_day, 3600);
    const int64_t minute = floor_divide(floor_modulo(seconds_of_day, 3600), 60);
    const int64_t second = floor_modulo(seconds_of_day, 60);
    char formatted[21] = {0};
    int written = 0;

    year += month <= 2 ? 1 : 0;
    if (month <= 0) {
        month += 12;
    }
    if (year < 0 || year > 9999) {
        return SHAULA_JSON_STATUS_TIMESTAMP_OUT_OF_RANGE;
    }

    written = g_snprintf(
        formatted,
        sizeof(formatted),
        "%04" G_GINT64_FORMAT "-%02" G_GINT64_FORMAT "-%02" G_GINT64_FORMAT
        "T%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT "Z",
        year,
        month,
        day,
        hour,
        minute,
        second);
    if (written != 20) {
        return SHAULA_JSON_STATUS_TIMESTAMP_OUT_OF_RANGE;
    }
    memcpy(output, formatted, 20U);
    return SHAULA_JSON_STATUS_OK;
}

ShaulaJsonSpan shaula_json_contract_version(void) {
    ShaulaJsonSpan result = {
        .data = contract_version,
        .length = sizeof(contract_version) - 1U,
    };
    return result;
}

void shaula_json_owned_bytes_init(ShaulaJsonOwnedBytes *output) {
    if (output == NULL) {
        return;
    }
    output->data = NULL;
    output->length = 0U;
}

void shaula_json_owned_bytes_clear(ShaulaJsonOwnedBytes *output) {
    if (output == NULL) {
        return;
    }
    g_free(output->data);
    output->data = NULL;
    output->length = 0U;
}

ShaulaJsonStatus shaula_json_timestamp_from_unix_seconds(
    int64_t epoch_seconds,
    ShaulaJsonOwnedBytes *output) {
    JsonBuilder builder = {0};
    uint8_t formatted[20] = {0};
    ShaulaJsonStatus status = SHAULA_JSON_STATUS_OK;

    if (output == NULL) {
        return SHAULA_JSON_STATUS_INVALID_ARGUMENT;
    }
    shaula_json_owned_bytes_clear(output);

    status = format_timestamp(epoch_seconds, formatted);
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_append(&builder, formatted, sizeof(formatted));
    }
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_finish(&builder, output);
    }
    builder_clear(&builder);
    return status;
}

ShaulaJsonStatus shaula_json_string_escape(
    ShaulaJsonSpan value,
    ShaulaJsonOwnedBytes *output) {
    JsonBuilder builder = {0};
    ShaulaJsonStatus status = SHAULA_JSON_STATUS_OK;

    if (output == NULL) {
        return SHAULA_JSON_STATUS_INVALID_ARGUMENT;
    }
    shaula_json_owned_bytes_clear(output);

    status = builder_append_escaped_string(&builder, value);
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_finish(&builder, output);
    }
    builder_clear(&builder);
    return status;
}

ShaulaJsonStatus shaula_json_nullable_string_escape(
    uint8_t has_value,
    ShaulaJsonSpan value,
    ShaulaJsonOwnedBytes *output) {
    JsonBuilder builder = {0};
    ShaulaJsonStatus status = SHAULA_JSON_STATUS_OK;

    if (output == NULL || has_value > 1U) {
        return SHAULA_JSON_STATUS_INVALID_ARGUMENT;
    }
    shaula_json_owned_bytes_clear(output);
    if (!span_is_valid(value)) {
        return SHAULA_JSON_STATUS_INVALID_ARGUMENT;
    }

    status = has_value == 0U
        ? builder_append_literal(&builder, "null")
        : builder_append_escaped_string(&builder, value);
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_finish(&builder, output);
    }
    builder_clear(&builder);
    return status;
}

ShaulaJsonStatus shaula_json_warnings_serialize(
    const ShaulaJsonSpan *warnings,
    size_t warning_count,
    ShaulaJsonOwnedBytes *output) {
    JsonBuilder builder = {0};
    ShaulaJsonStatus status = SHAULA_JSON_STATUS_OK;
    size_t index = 0U;

    if (output == NULL) {
        return SHAULA_JSON_STATUS_INVALID_ARGUMENT;
    }
    shaula_json_owned_bytes_clear(output);
    if (warnings == NULL && warning_count != 0U) {
        return SHAULA_JSON_STATUS_INVALID_ARGUMENT;
    }

    status = builder_append_byte(&builder, '[');
    for (index = 0U; status == SHAULA_JSON_STATUS_OK && index < warning_count; index += 1U) {
        if (index != 0U) {
            status = builder_append_byte(&builder, ',');
        }
        if (status == SHAULA_JSON_STATUS_OK) {
            status = builder_append_escaped_string(&builder, warnings[index]);
        }
    }
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_append_byte(&builder, ']');
    }
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_finish(&builder, output);
    }
    builder_clear(&builder);
    return status;
}

ShaulaJsonStatus shaula_json_basic_error_build(
    int64_t epoch_seconds,
    ShaulaJsonSpan command,
    ShaulaJsonSpan code,
    ShaulaJsonSpan message,
    uint8_t retryable,
    ShaulaJsonSpan details_json,
    ShaulaJsonOwnedBytes *output) {
    JsonBuilder builder = {0};
    uint8_t timestamp[20] = {0};
    const ShaulaJsonSpan timestamp_span = {
        .data = timestamp,
        .length = sizeof(timestamp),
    };
    ShaulaJsonStatus status = SHAULA_JSON_STATUS_OK;

    if (output == NULL) {
        return SHAULA_JSON_STATUS_INVALID_ARGUMENT;
    }
    shaula_json_owned_bytes_clear(output);
    if (!span_is_valid(command) || !span_is_valid(code) || !span_is_valid(message) ||
        !span_is_valid(details_json)) {
        return SHAULA_JSON_STATUS_INVALID_ARGUMENT;
    }

    status = format_timestamp(epoch_seconds, timestamp);
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_append_literal(
            &builder,
            "{\"ok\":false,\"contract_version\":\"1.0.0\",\"command\":");
    }
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_append_escaped_string(&builder, command);
    }
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_append_literal(&builder, ",\"timestamp\":");
    }
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_append_escaped_string(&builder, timestamp_span);
    }
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_append_literal(&builder, ",\"error\":{\"code\":");
    }
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_append_escaped_string(&builder, code);
    }
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_append_literal(&builder, ",\"message\":");
    }
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_append_escaped_string(&builder, message);
    }
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_append_literal(
            &builder,
            retryable ? ",\"retryable\":true,\"details\":"
                      : ",\"retryable\":false,\"details\":");
    }
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_append(&builder, details_json.data, details_json.length);
    }
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_append_literal(&builder, "},\"warnings\":[]}\n");
    }
    if (status == SHAULA_JSON_STATUS_OK) {
        status = builder_finish(&builder, output);
    }
    builder_clear(&builder);
    return status;
}
