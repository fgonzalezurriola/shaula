#include "notify/request.h"

#include <glib.h>
#include <limits.h>
#include <string.h>

#define SPAN_LITERAL(value)                                                   \
  { (const uint8_t *)(value), sizeof(value) - 1U }

static const ShaulaNotifySpan urgency_tokens[] = {
    SPAN_LITERAL("low"),
    SPAN_LITERAL("normal"),
    SPAN_LITERAL("critical"),
};

static const ShaulaNotifySpan arg_notify_send = SPAN_LITERAL("notify-send");
static const ShaulaNotifySpan arg_app_name =
    SPAN_LITERAL("--app-name=Shaula");
static const ShaulaNotifySpan arg_urgency = SPAN_LITERAL("--urgency");
static const ShaulaNotifySpan arg_expire_time = SPAN_LITERAL("--expire-time");
static const ShaulaNotifySpan arg_transient = SPAN_LITERAL("--transient");
static const ShaulaNotifySpan arg_hint = SPAN_LITERAL("--hint");
static const ShaulaNotifySpan arg_icon = SPAN_LITERAL("-i");
static const ShaulaNotifySpan image_hint_prefix =
    SPAN_LITERAL("string:image-path:");
static const ShaulaNotifySpan action_prefix = SPAN_LITERAL("--action=");
static const ShaulaNotifySpan equals = SPAN_LITERAL("=");
static const ShaulaNotifySpan file_uri_prefix = SPAN_LITERAL("file://");

static gboolean span_is_valid(ShaulaNotifySpan span) {
  return span.data != NULL || span.length == 0U;
}

static gboolean flag_is_valid(uint8_t value) { return value == 0U || value == 1U; }

static gboolean urgency_is_valid(ShaulaNotifyUrgency urgency) {
  return urgency >= SHAULA_NOTIFY_URGENCY_LOW &&
         urgency <= SHAULA_NOTIFY_URGENCY_CRITICAL;
}

static gboolean image_mode_is_valid(ShaulaNotifyImageMode image_mode) {
  return image_mode >= SHAULA_NOTIFY_IMAGE_MODE_HINT &&
         image_mode <= SHAULA_NOTIFY_IMAGE_MODE_ICON;
}

static gboolean checked_add_size(size_t left, size_t right, size_t *result) {
  if (right > SIZE_MAX - left) {
    return FALSE;
  }
  *result = left + right;
  return TRUE;
}

static ShaulaNotifyStatus owned_join(const ShaulaNotifySpan *parts,
                                     size_t part_count,
                                     ShaulaNotifyOwnedBytes *output) {
  size_t total = 0U;
  size_t allocation_size = 0U;
  size_t offset = 0U;
  size_t index = 0U;
  uint8_t *data = NULL;

  if (parts == NULL || output == NULL) {
    return SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT;
  }
  for (index = 0U; index < part_count; index += 1U) {
    if (!span_is_valid(parts[index])) {
      return SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT;
    }
    if (!checked_add_size(total, parts[index].length, &total)) {
      return SHAULA_NOTIFY_STATUS_SIZE_OVERFLOW;
    }
  }
  if (!checked_add_size(total, 1U, &allocation_size)) {
    return SHAULA_NOTIFY_STATUS_SIZE_OVERFLOW;
  }

  data = g_try_malloc(allocation_size);
  if (data == NULL) {
    return SHAULA_NOTIFY_STATUS_OUT_OF_MEMORY;
  }
  for (index = 0U; index < part_count; index += 1U) {
    if (parts[index].length != 0U) {
      memcpy(data + offset, parts[index].data, parts[index].length);
      offset += parts[index].length;
    }
  }
  data[total] = 0U;
  output->data = data;
  output->length = total;
  return SHAULA_NOTIFY_STATUS_OK;
}

static ShaulaNotifyStatus append_arg(ShaulaNotifySendArgs *args,
                                     ShaulaNotifySpan value) {
  if (args == NULL || !span_is_valid(value)) {
    return SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT;
  }
  if (args->length >= SHAULA_NOTIFY_SEND_ARG_CAPACITY) {
    return SHAULA_NOTIFY_STATUS_SIZE_OVERFLOW;
  }
  args->items[args->length] = value;
  args->length += 1U;
  return SHAULA_NOTIFY_STATUS_OK;
}

static gboolean uri_byte_is_literal(uint8_t value) {
  return value == (uint8_t)'/' || value == (uint8_t)'-' ||
         value == (uint8_t)'_' || value == (uint8_t)'.' ||
         value == (uint8_t)'~' ||
         (value >= (uint8_t)'0' && value <= (uint8_t)'9') ||
         (value >= (uint8_t)'A' && value <= (uint8_t)'Z') ||
         (value >= (uint8_t)'a' && value <= (uint8_t)'z');
}

void shaula_notify_request_init(ShaulaNotifyRequest *request) {
  if (request == NULL) {
    return;
  }
  memset(request, 0, sizeof(*request));
  request->urgency = SHAULA_NOTIFY_URGENCY_NORMAL;
  request->timeout_ms = 2500U;
  request->transient = 1U;
}

ShaulaNotifySpan shaula_notify_urgency_token(ShaulaNotifyUrgency urgency) {
  if (!urgency_is_valid(urgency)) {
    return (ShaulaNotifySpan){NULL, 0U};
  }
  return urgency_tokens[(size_t)urgency];
}

void shaula_notify_owned_bytes_init(ShaulaNotifyOwnedBytes *output) {
  if (output == NULL) {
    return;
  }
  output->data = NULL;
  output->length = 0U;
}

void shaula_notify_owned_bytes_clear(ShaulaNotifyOwnedBytes *output) {
  if (output == NULL) {
    return;
  }
  g_free(output->data);
  output->data = NULL;
  output->length = 0U;
}

void shaula_notify_send_args_init(ShaulaNotifySendArgs *output) {
  if (output == NULL) {
    return;
  }
  memset(output, 0, sizeof(*output));
}

void shaula_notify_send_args_clear(ShaulaNotifySendArgs *output) {
  if (output == NULL) {
    return;
  }
  shaula_notify_owned_bytes_clear(&output->timeout);
  shaula_notify_owned_bytes_clear(&output->image_hint);
  shaula_notify_owned_bytes_clear(&output->action_arg);
  memset(output->items, 0, sizeof(output->items));
  output->length = 0U;
}

ShaulaNotifyStatus shaula_notify_file_uri_build(
    ShaulaNotifySpan path,
    ShaulaNotifyOwnedBytes *output) {
  static const uint8_t hex[] = "0123456789ABCDEF";
  size_t maximum_encoded = 0U;
  size_t maximum_with_prefix = 0U;
  size_t maximum_allocation = 0U;
  size_t encoded_length = 0U;
  size_t total_length = 0U;
  size_t allocation_size = 0U;
  size_t index = 0U;
  size_t offset = 0U;
  uint8_t *data = NULL;

  if (output == NULL) {
    return SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT;
  }
  shaula_notify_owned_bytes_clear(output);
  if (!span_is_valid(path)) {
    return SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT;
  }

  if (path.length > SIZE_MAX / 3U) {
    return SHAULA_NOTIFY_STATUS_SIZE_OVERFLOW;
  }
  maximum_encoded = path.length * 3U;
  if (!checked_add_size(file_uri_prefix.length, maximum_encoded,
                        &maximum_with_prefix) ||
      !checked_add_size(maximum_with_prefix, 1U, &maximum_allocation)) {
    return SHAULA_NOTIFY_STATUS_SIZE_OVERFLOW;
  }
  (void)maximum_allocation;

  for (index = 0U; index < path.length; index += 1U) {
    const size_t width = uri_byte_is_literal(path.data[index]) ? 1U : 3U;
    if (!checked_add_size(encoded_length, width, &encoded_length)) {
      return SHAULA_NOTIFY_STATUS_SIZE_OVERFLOW;
    }
  }
  if (!checked_add_size(file_uri_prefix.length, encoded_length, &total_length) ||
      !checked_add_size(total_length, 1U, &allocation_size)) {
    return SHAULA_NOTIFY_STATUS_SIZE_OVERFLOW;
  }

  data = g_try_malloc(allocation_size);
  if (data == NULL) {
    return SHAULA_NOTIFY_STATUS_OUT_OF_MEMORY;
  }
  memcpy(data, file_uri_prefix.data, file_uri_prefix.length);
  offset = file_uri_prefix.length;
  for (index = 0U; index < path.length; index += 1U) {
    const uint8_t value = path.data[index];
    if (uri_byte_is_literal(value)) {
      data[offset] = value;
      offset += 1U;
    } else {
      data[offset] = (uint8_t)'%';
      data[offset + 1U] = hex[(value >> 4U) & 0x0fU];
      data[offset + 2U] = hex[value & 0x0fU];
      offset += 3U;
    }
  }
  data[total_length] = 0U;
  output->data = data;
  output->length = total_length;
  return SHAULA_NOTIFY_STATUS_OK;
}

ShaulaNotifyStatus shaula_notify_send_args_build(
    const ShaulaNotifyRequest *request,
    ShaulaNotifyImageMode image_mode,
    ShaulaNotifySendArgs *output) {
  ShaulaNotifySendArgs built = {0};
  ShaulaNotifyOwnedBytes file_uri = {0};
  ShaulaNotifySpan timeout_part = {0};
  ShaulaNotifySpan image_parts[2] = {0};
  ShaulaNotifySpan action_parts[4] = {0};
  ShaulaNotifySpan urgency_token = {0};
  char timeout_buffer[11] = {0};
  int timeout_length = 0;
  ShaulaNotifyStatus status = SHAULA_NOTIFY_STATUS_OK;

  if (output == NULL) {
    return SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT;
  }
  shaula_notify_send_args_clear(output);
  if (request == NULL || !span_is_valid(request->summary) ||
      !span_is_valid(request->body) || !span_is_valid(request->image_path) ||
      !span_is_valid(request->action_id) ||
      !span_is_valid(request->action_label) ||
      !flag_is_valid(request->has_image_path) ||
      !flag_is_valid(request->transient) ||
      !flag_is_valid(request->has_action)) {
    return SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT;
  }
  if (!urgency_is_valid(request->urgency)) {
    return SHAULA_NOTIFY_STATUS_INVALID_URGENCY;
  }
  if (!image_mode_is_valid(image_mode)) {
    return SHAULA_NOTIFY_STATUS_INVALID_IMAGE_MODE;
  }

  urgency_token = shaula_notify_urgency_token(request->urgency);
  timeout_length = g_snprintf(timeout_buffer, sizeof(timeout_buffer), "%u",
                              request->timeout_ms);
  if (timeout_length < 1 || (size_t)timeout_length >= sizeof(timeout_buffer)) {
    return SHAULA_NOTIFY_STATUS_SIZE_OVERFLOW;
  }
  timeout_part = (ShaulaNotifySpan){
      .data = (const uint8_t *)timeout_buffer,
      .length = (size_t)timeout_length,
  };
  status = owned_join(&timeout_part, 1U, &built.timeout);
  if (status != SHAULA_NOTIFY_STATUS_OK) {
    goto fail;
  }

#define APPEND(value)                                                         \
  do {                                                                        \
    status = append_arg(&built, (value));                                     \
    if (status != SHAULA_NOTIFY_STATUS_OK) {                                  \
      goto fail;                                                              \
    }                                                                         \
  } while (0)

  APPEND(arg_notify_send);
  APPEND(arg_app_name);
  APPEND(arg_urgency);
  APPEND(urgency_token);
  APPEND(arg_expire_time);
  APPEND(((ShaulaNotifySpan){built.timeout.data, built.timeout.length}));

  if (request->transient != 0U) {
    APPEND(arg_transient);
  }

  if (request->has_image_path != 0U) {
    if (image_mode == SHAULA_NOTIFY_IMAGE_MODE_HINT) {
      status = shaula_notify_file_uri_build(request->image_path, &file_uri);
      if (status != SHAULA_NOTIFY_STATUS_OK) {
        goto fail;
      }
      image_parts[0] = image_hint_prefix;
      image_parts[1] =
          (ShaulaNotifySpan){file_uri.data, file_uri.length};
      status = owned_join(image_parts, 2U, &built.image_hint);
      if (status != SHAULA_NOTIFY_STATUS_OK) {
        goto fail;
      }
      APPEND(arg_hint);
      APPEND(((ShaulaNotifySpan){built.image_hint.data,
                                 built.image_hint.length}));
    } else {
      APPEND(arg_icon);
      APPEND(request->image_path);
    }
  }

  if (request->has_action != 0U) {
    action_parts[0] = action_prefix;
    action_parts[1] = request->action_id;
    action_parts[2] = equals;
    action_parts[3] = request->action_label;
    status = owned_join(action_parts, 4U, &built.action_arg);
    if (status != SHAULA_NOTIFY_STATUS_OK) {
      goto fail;
    }
    APPEND(((ShaulaNotifySpan){built.action_arg.data,
                               built.action_arg.length}));
  }

  APPEND(request->summary);
  APPEND(request->body);

#undef APPEND

  shaula_notify_owned_bytes_clear(&file_uri);
  *output = built;
  return SHAULA_NOTIFY_STATUS_OK;

fail:
#ifdef APPEND
#undef APPEND
#endif
  shaula_notify_owned_bytes_clear(&file_uri);
  shaula_notify_send_args_clear(&built);
  return status;
}
