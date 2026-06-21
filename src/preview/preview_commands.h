#ifndef SHAULA_PREVIEW_COMMANDS_H
#define SHAULA_PREVIEW_COMMANDS_H

#include <gtk/gtk.h>

#include "preview_state.h"

typedef enum {
  SHAULA_PREVIEW_COMMAND_COPY,
  SHAULA_PREVIEW_COMMAND_COPY_SELECTED_ANNOTATION,
  SHAULA_PREVIEW_COMMAND_CUT_SELECTED_ANNOTATION,
  SHAULA_PREVIEW_COMMAND_PASTE_ANNOTATION,
  SHAULA_PREVIEW_COMMAND_SAVE,
  SHAULA_PREVIEW_COMMAND_SAVE_AS,
  SHAULA_PREVIEW_COMMAND_DONE,
  SHAULA_PREVIEW_COMMAND_UNDO,
  SHAULA_PREVIEW_COMMAND_REDO,
  SHAULA_PREVIEW_COMMAND_DUPLICATE_SELECTED,
  SHAULA_PREVIEW_COMMAND_DELETE_SELECTED,
  SHAULA_PREVIEW_COMMAND_SELECT_ALL_ANNOTATIONS,
  SHAULA_PREVIEW_COMMAND_CROP_SELECTED,
  SHAULA_PREVIEW_COMMAND_BLUR_REGION,
  SHAULA_PREVIEW_COMMAND_ERASE_REGION,
  SHAULA_PREVIEW_COMMAND_SPOTLIGHT_REGION,
  SHAULA_PREVIEW_COMMAND_RESET_ANNOTATIONS,
  SHAULA_PREVIEW_COMMAND_COPY_PATH,
  SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR,
  SHAULA_PREVIEW_COMMAND_USE_HOVER_COLOR,
  SHAULA_PREVIEW_COMMAND_OPEN_CONTAINING_FOLDER,
  SHAULA_PREVIEW_COMMAND_CLOSE,
  SHAULA_PREVIEW_COMMAND_DISCARD,
  SHAULA_PREVIEW_COMMAND_FIT_TO_SCREEN,
  SHAULA_PREVIEW_COMMAND_ACTUAL_SIZE,
  SHAULA_PREVIEW_COMMAND_ZOOM_IN,
  SHAULA_PREVIEW_COMMAND_ZOOM_OUT,
  SHAULA_PREVIEW_COMMAND_SET_TOOL_SELECT,
  SHAULA_PREVIEW_COMMAND_SET_TOOL_HAND,
  SHAULA_PREVIEW_COMMAND_SET_TOOL_CROP,
  SHAULA_PREVIEW_COMMAND_SET_TOOL_ERASER,
  SHAULA_PREVIEW_COMMAND_SET_TOOL_ARROW,
  SHAULA_PREVIEW_COMMAND_SET_TOOL_LINE,
  SHAULA_PREVIEW_COMMAND_SET_TOOL_TEXT,
  SHAULA_PREVIEW_COMMAND_SET_TOOL_MEASURE,
  SHAULA_PREVIEW_COMMAND_SET_TOOL_RECTANGLE,
  SHAULA_PREVIEW_COMMAND_SET_TOOL_HIGHLIGHT,
  SHAULA_PREVIEW_COMMAND_SET_TOOL_PEN,
  SHAULA_PREVIEW_COMMAND_SET_TOOL_SPOTLIGHT,
} ShaulaPreviewCommand;

typedef struct {
  guint keyval;
  GdkModifierType modifiers;
  ShaulaPreviewCommand command;
} ShaulaPreviewShortcut;

gboolean shaula_preview_command_available(ShaulaPreviewState *state,
                                          ShaulaPreviewCommand command);
gboolean shaula_preview_execute_command(ShaulaPreviewState *state,
                                        ShaulaPreviewCommand command);
gboolean shaula_preview_shortcut_command(guint keyval,
                                         GdkModifierType modifiers,
                                         ShaulaPreviewCommand *command);
gboolean shaula_preview_command_for_tool(ShaulaTool tool,
                                         ShaulaPreviewCommand *command);
const char *shaula_preview_command_shortcut_label(
    ShaulaPreviewCommand command);
/* Returns the shared toolbar and overflow shortcut hint for a tool. */
const char *shaula_preview_tool_shortcut_hint(ShaulaTool tool);
const char *shaula_preview_tool_shortcut_badge(ShaulaTool tool);

#endif
