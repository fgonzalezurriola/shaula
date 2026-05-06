#include "preview_commands.h"

#include "preview_actions.h"

static const GdkModifierType shortcut_modifiers =
    GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_ALT_MASK | GDK_SUPER_MASK;

static const ShaulaPreviewShortcut shortcuts[] = {
    {GDK_KEY_c, GDK_CONTROL_MASK, SHAULA_PREVIEW_COMMAND_COPY},
    {GDK_KEY_s, GDK_CONTROL_MASK, SHAULA_PREVIEW_COMMAND_SAVE_AS},
    {GDK_KEY_z, GDK_CONTROL_MASK, SHAULA_PREVIEW_COMMAND_UNDO},
    {GDK_KEY_z, GDK_CONTROL_MASK | GDK_SHIFT_MASK,
     SHAULA_PREVIEW_COMMAND_REDO},
    {GDK_KEY_y, GDK_CONTROL_MASK, SHAULA_PREVIEW_COMMAND_REDO},
    {GDK_KEY_d, GDK_CONTROL_MASK, SHAULA_PREVIEW_COMMAND_DUPLICATE_SELECTED},
    {GDK_KEY_Delete, 0, SHAULA_PREVIEW_COMMAND_DELETE_SELECTED},
    {GDK_KEY_BackSpace, 0, SHAULA_PREVIEW_COMMAND_DELETE_SELECTED},
    {GDK_KEY_Tab, 0, SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR},
    {GDK_KEY_f, 0, SHAULA_PREVIEW_COMMAND_FIT_TO_SCREEN},
    {GDK_KEY_0, 0, SHAULA_PREVIEW_COMMAND_ACTUAL_SIZE},
};

static ShaulaTool tool_for_command(ShaulaPreviewCommand command,
                                   gboolean *is_tool_command) {
  *is_tool_command = TRUE;
  switch (command) {
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_SELECT:
    return SHAULA_TOOL_SELECT;
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_CROP:
    return SHAULA_TOOL_CROP;
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_ARROW:
    return SHAULA_TOOL_ARROW;
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_TEXT:
    return SHAULA_TOOL_TEXT;
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_MEASURE:
    return SHAULA_TOOL_MEASURE;
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_RECTANGLE:
    return SHAULA_TOOL_RECTANGLE;
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_HIGHLIGHT:
    return SHAULA_TOOL_HIGHLIGHT;
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_PEN:
    return SHAULA_TOOL_PEN;
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_SPOTLIGHT:
    return SHAULA_TOOL_SPOTLIGHT;
  case SHAULA_PREVIEW_COMMAND_COPY:
  case SHAULA_PREVIEW_COMMAND_SAVE_AS:
  case SHAULA_PREVIEW_COMMAND_UNDO:
  case SHAULA_PREVIEW_COMMAND_REDO:
  case SHAULA_PREVIEW_COMMAND_DUPLICATE_SELECTED:
  case SHAULA_PREVIEW_COMMAND_DELETE_SELECTED:
  case SHAULA_PREVIEW_COMMAND_CROP_SELECTED:
  case SHAULA_PREVIEW_COMMAND_BLUR_REGION:
  case SHAULA_PREVIEW_COMMAND_ERASE_REGION:
  case SHAULA_PREVIEW_COMMAND_SPOTLIGHT_REGION:
  case SHAULA_PREVIEW_COMMAND_RESET_ANNOTATIONS:
  case SHAULA_PREVIEW_COMMAND_COPY_PATH:
  case SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR:
  case SHAULA_PREVIEW_COMMAND_OPEN_CONTAINING_FOLDER:
  case SHAULA_PREVIEW_COMMAND_DISCARD:
  case SHAULA_PREVIEW_COMMAND_FIT_TO_SCREEN:
  case SHAULA_PREVIEW_COMMAND_ACTUAL_SIZE:
    *is_tool_command = FALSE;
    return SHAULA_TOOL_SELECT;
  }
  *is_tool_command = FALSE;
  return SHAULA_TOOL_SELECT;
}

gboolean shaula_preview_command_available(ShaulaPreviewState *state,
                                          ShaulaPreviewCommand command) {
  if (state == NULL)
    return FALSE;

  switch (command) {
  case SHAULA_PREVIEW_COMMAND_UNDO:
    return shaula_preview_can_undo(state);
  case SHAULA_PREVIEW_COMMAND_REDO:
    return shaula_preview_can_redo(state);
  case SHAULA_PREVIEW_COMMAND_DUPLICATE_SELECTED:
    return shaula_preview_can_duplicate_selected(state);
  case SHAULA_PREVIEW_COMMAND_DELETE_SELECTED:
    return shaula_preview_can_delete_selected(state);
  case SHAULA_PREVIEW_COMMAND_CROP_SELECTED:
    if (state->active_tool == SHAULA_TOOL_SELECT &&
        state->has_region_selection)
      return TRUE;
    if (state->active_tool == SHAULA_TOOL_SELECT &&
        state->selected_annotation != NULL) {
      ShaulaAnnotation *annotation = state->selected_annotation;
      return annotation->type == SHAULA_ANNOTATION_RECTANGLE ||
             annotation->type == SHAULA_ANNOTATION_HIGHLIGHT;
    }
    return FALSE;
  case SHAULA_PREVIEW_COMMAND_BLUR_REGION:
  case SHAULA_PREVIEW_COMMAND_ERASE_REGION:
  case SHAULA_PREVIEW_COMMAND_SPOTLIGHT_REGION:
    return state->active_tool == SHAULA_TOOL_SELECT &&
           state->has_region_selection && state->image != NULL;
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_CROP:
    return state->image != NULL;
  case SHAULA_PREVIEW_COMMAND_COPY:
  case SHAULA_PREVIEW_COMMAND_SAVE_AS:
  case SHAULA_PREVIEW_COMMAND_FIT_TO_SCREEN:
  case SHAULA_PREVIEW_COMMAND_ACTUAL_SIZE:
    return state->image != NULL;
  case SHAULA_PREVIEW_COMMAND_RESET_ANNOTATIONS:
    return state->annotations != NULL && state->annotations->len > 0;
  case SHAULA_PREVIEW_COMMAND_COPY_PATH:
  case SHAULA_PREVIEW_COMMAND_OPEN_CONTAINING_FOLDER:
    return state->path != NULL;
  case SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR:
    return state->image != NULL;
  case SHAULA_PREVIEW_COMMAND_DISCARD:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_SELECT:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_ARROW:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_TEXT:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_MEASURE:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_RECTANGLE:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_HIGHLIGHT:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_PEN:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_SPOTLIGHT:
    return TRUE;
  }
  return FALSE;
}

gboolean shaula_preview_execute_command(ShaulaPreviewState *state,
                                        ShaulaPreviewCommand command) {
  if (!shaula_preview_command_available(state, command))
    return FALSE;

  gboolean is_tool_command = FALSE;
  ShaulaTool tool = tool_for_command(command, &is_tool_command);
  if (is_tool_command) {
    shaula_preview_action_set_tool(state, tool);
    return TRUE;
  }

  switch (command) {
  case SHAULA_PREVIEW_COMMAND_COPY:
    shaula_preview_action_copy(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_SAVE_AS:
    shaula_preview_action_save_as(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_UNDO:
    return shaula_preview_undo(state);
  case SHAULA_PREVIEW_COMMAND_REDO:
    return shaula_preview_redo(state);
  case SHAULA_PREVIEW_COMMAND_DUPLICATE_SELECTED:
    return shaula_preview_duplicate_selected(state);
  case SHAULA_PREVIEW_COMMAND_DELETE_SELECTED:
    shaula_preview_delete_selected(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_CROP_SELECTED:
    if (state->has_region_selection)
      return shaula_preview_apply_crop_to_region_selection(state);
    return shaula_preview_apply_crop_to_selected_rect(state);
  case SHAULA_PREVIEW_COMMAND_BLUR_REGION:
    return shaula_preview_blur_region_selection(state);
  case SHAULA_PREVIEW_COMMAND_ERASE_REGION:
    return shaula_preview_erase_region_selection(state);
  case SHAULA_PREVIEW_COMMAND_SPOTLIGHT_REGION:
    return shaula_preview_spotlight_region_selection(state);
  case SHAULA_PREVIEW_COMMAND_RESET_ANNOTATIONS:
    shaula_preview_reset_annotations(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_COPY_PATH:
    shaula_preview_action_copy_path(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR:
    shaula_preview_action_copy_hover_color(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_OPEN_CONTAINING_FOLDER:
    shaula_preview_action_open_containing_folder(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_DISCARD:
    shaula_preview_action_discard(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_FIT_TO_SCREEN:
    shaula_preview_action_fit(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_ACTUAL_SIZE:
    shaula_preview_action_actual_size(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_SELECT:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_CROP:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_ARROW:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_TEXT:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_MEASURE:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_RECTANGLE:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_HIGHLIGHT:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_PEN:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_SPOTLIGHT:
    return FALSE;
  }
  return FALSE;
}

gboolean shaula_preview_shortcut_command(guint keyval,
                                         GdkModifierType modifiers,
                                         ShaulaPreviewCommand *command) {
  GdkModifierType normalized = modifiers & shortcut_modifiers;
  for (guint i = 0; i < G_N_ELEMENTS(shortcuts); i++) {
    if (shortcuts[i].keyval == keyval &&
        shortcuts[i].modifiers == normalized) {
      if (command != NULL)
        *command = shortcuts[i].command;
      return TRUE;
    }
  }
  return FALSE;
}

const char *shaula_preview_command_shortcut_label(
    ShaulaPreviewCommand command) {
  switch (command) {
  case SHAULA_PREVIEW_COMMAND_COPY:
    return "Ctrl+C";
  case SHAULA_PREVIEW_COMMAND_SAVE_AS:
    return "Ctrl+S";
  case SHAULA_PREVIEW_COMMAND_UNDO:
    return "Ctrl+Z";
  case SHAULA_PREVIEW_COMMAND_REDO:
    return "Ctrl+Shift+Z";
  case SHAULA_PREVIEW_COMMAND_DUPLICATE_SELECTED:
    return "Ctrl+D";
  case SHAULA_PREVIEW_COMMAND_DELETE_SELECTED:
    return "Delete";
  case SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR:
    return "Tab";
  case SHAULA_PREVIEW_COMMAND_FIT_TO_SCREEN:
    return "f";
  case SHAULA_PREVIEW_COMMAND_ACTUAL_SIZE:
    return "0";
  case SHAULA_PREVIEW_COMMAND_RESET_ANNOTATIONS:
  case SHAULA_PREVIEW_COMMAND_CROP_SELECTED:
  case SHAULA_PREVIEW_COMMAND_BLUR_REGION:
  case SHAULA_PREVIEW_COMMAND_ERASE_REGION:
  case SHAULA_PREVIEW_COMMAND_SPOTLIGHT_REGION:
  case SHAULA_PREVIEW_COMMAND_COPY_PATH:
  case SHAULA_PREVIEW_COMMAND_OPEN_CONTAINING_FOLDER:
  case SHAULA_PREVIEW_COMMAND_DISCARD:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_SELECT:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_CROP:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_ARROW:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_TEXT:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_MEASURE:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_RECTANGLE:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_HIGHLIGHT:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_PEN:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_SPOTLIGHT:
    return NULL;
  }
  return NULL;
}
