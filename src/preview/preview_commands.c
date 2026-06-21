#include "preview_commands.h"

#include "preview_actions.h"

static const GdkModifierType shortcut_modifiers =
    GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_ALT_MASK | GDK_SUPER_MASK;

typedef struct {
  ShaulaPreviewCommand command;
  gboolean is_tool_command;
  ShaulaTool tool;
  guint keyval;
  GdkModifierType modifiers;
  const char *shortcut_label;
} ShaulaPreviewCommandSpec;

static const char *const tool_shortcut_hints[SHAULA_TOOL_COUNT] = {
    [SHAULA_TOOL_SELECT] = "V, 1",
    [SHAULA_TOOL_HAND] = "hold Space",
    [SHAULA_TOOL_ERASER] = "E, 0",
    [SHAULA_TOOL_ARROW] = "A, 3",
    [SHAULA_TOOL_LINE] = "L, 4",
    [SHAULA_TOOL_TEXT] = "T, 5",
    [SHAULA_TOOL_MEASURE] = "8",
    [SHAULA_TOOL_RECTANGLE] = "R, 2",
    [SHAULA_TOOL_HIGHLIGHT] = "H, 7",
    [SHAULA_TOOL_PEN] = "P/X, 6",
    [SHAULA_TOOL_SPOTLIGHT] = "9",
};

static const char *const tool_shortcut_badges[SHAULA_TOOL_COUNT] = {
    [SHAULA_TOOL_SELECT] = "1",
    [SHAULA_TOOL_ERASER] = "0",
    [SHAULA_TOOL_ARROW] = "3",
    [SHAULA_TOOL_LINE] = "4",
    [SHAULA_TOOL_TEXT] = "5",
    [SHAULA_TOOL_MEASURE] = "8",
    [SHAULA_TOOL_RECTANGLE] = "2",
    [SHAULA_TOOL_HIGHLIGHT] = "7",
    [SHAULA_TOOL_PEN] = "6",
    [SHAULA_TOOL_SPOTLIGHT] = "9",
};

static const ShaulaPreviewCommandSpec command_specs[] = {
    {SHAULA_PREVIEW_COMMAND_COPY, FALSE, SHAULA_TOOL_SELECT, GDK_KEY_c,
     GDK_CONTROL_MASK, "Ctrl+C"},
    {SHAULA_PREVIEW_COMMAND_CUT_SELECTED_ANNOTATION, FALSE, SHAULA_TOOL_SELECT,
     GDK_KEY_x, GDK_CONTROL_MASK, "Ctrl+X"},
    {SHAULA_PREVIEW_COMMAND_PASTE_ANNOTATION, FALSE, SHAULA_TOOL_SELECT,
     GDK_KEY_v, GDK_CONTROL_MASK, "Ctrl+V"},
    {SHAULA_PREVIEW_COMMAND_SAVE, FALSE, SHAULA_TOOL_SELECT, GDK_KEY_s,
     GDK_CONTROL_MASK, "Ctrl+S"},
    {SHAULA_PREVIEW_COMMAND_SAVE_AS, FALSE, SHAULA_TOOL_SELECT, GDK_KEY_s,
     GDK_CONTROL_MASK | GDK_SHIFT_MASK, "Ctrl+Shift+S"},
    {SHAULA_PREVIEW_COMMAND_SAVE_AS, FALSE, SHAULA_TOOL_SELECT, GDK_KEY_S,
     GDK_CONTROL_MASK | GDK_SHIFT_MASK, "Ctrl+Shift+S"},
    {SHAULA_PREVIEW_COMMAND_DONE, FALSE, SHAULA_TOOL_SELECT, 0, 0, NULL},
    {SHAULA_PREVIEW_COMMAND_UNDO, FALSE, SHAULA_TOOL_SELECT, GDK_KEY_z,
     GDK_CONTROL_MASK, "Ctrl+Z"},
    {SHAULA_PREVIEW_COMMAND_REDO, FALSE, SHAULA_TOOL_SELECT, GDK_KEY_z,
     GDK_CONTROL_MASK | GDK_SHIFT_MASK, "Ctrl+Shift+Z"},
    {SHAULA_PREVIEW_COMMAND_REDO, FALSE, SHAULA_TOOL_SELECT, GDK_KEY_Z,
     GDK_CONTROL_MASK | GDK_SHIFT_MASK, "Ctrl+Shift+Z"},
    {SHAULA_PREVIEW_COMMAND_REDO, FALSE, SHAULA_TOOL_SELECT, GDK_KEY_y,
     GDK_CONTROL_MASK, "Ctrl+Y"},
    {SHAULA_PREVIEW_COMMAND_DUPLICATE_SELECTED, FALSE, SHAULA_TOOL_SELECT,
     GDK_KEY_d, GDK_CONTROL_MASK, "Ctrl+D"},
    {SHAULA_PREVIEW_COMMAND_DELETE_SELECTED, FALSE, SHAULA_TOOL_SELECT,
     GDK_KEY_Delete, 0, "Delete"},
    {SHAULA_PREVIEW_COMMAND_SELECT_ALL_ANNOTATIONS, FALSE, SHAULA_TOOL_SELECT,
     GDK_KEY_a, GDK_CONTROL_MASK, "Ctrl+A"},
    {SHAULA_PREVIEW_COMMAND_SELECT_ALL_ANNOTATIONS, FALSE, SHAULA_TOOL_SELECT,
     GDK_KEY_A, GDK_CONTROL_MASK | GDK_SHIFT_MASK, "Ctrl+A"},
    {SHAULA_PREVIEW_COMMAND_SELECT_ALL_ANNOTATIONS, FALSE, SHAULA_TOOL_SELECT,
     GDK_KEY_a, GDK_SUPER_MASK, "Super+A"},
    {SHAULA_PREVIEW_COMMAND_SELECT_ALL_ANNOTATIONS, FALSE, SHAULA_TOOL_SELECT,
     GDK_KEY_A, GDK_SUPER_MASK | GDK_SHIFT_MASK, "Super+A"},
    {SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR, FALSE, SHAULA_TOOL_SELECT,
     GDK_KEY_Tab, 0, "Tab"},
    {SHAULA_PREVIEW_COMMAND_FIT_TO_SCREEN, FALSE, SHAULA_TOOL_SELECT,
     GDK_KEY_f, 0, "F"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_ERASER, TRUE, SHAULA_TOOL_ERASER,
     GDK_KEY_0, 0, "0"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_ERASER, TRUE, SHAULA_TOOL_ERASER,
     GDK_KEY_KP_0, 0, "0"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_ERASER, TRUE, SHAULA_TOOL_ERASER,
     GDK_KEY_e, 0, "E"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_SELECT, TRUE, SHAULA_TOOL_SELECT,
     GDK_KEY_1, 0, "1"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_SELECT, TRUE, SHAULA_TOOL_SELECT,
     GDK_KEY_KP_1, 0, "1"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_SELECT, TRUE, SHAULA_TOOL_SELECT,
     GDK_KEY_v, 0, "V"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_RECTANGLE, TRUE, SHAULA_TOOL_RECTANGLE,
     GDK_KEY_2, 0, "2"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_RECTANGLE, TRUE, SHAULA_TOOL_RECTANGLE,
     GDK_KEY_KP_2, 0, "2"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_ARROW, TRUE, SHAULA_TOOL_ARROW,
     GDK_KEY_a, 0, "A"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_ARROW, TRUE, SHAULA_TOOL_ARROW,
     GDK_KEY_3, 0, "3"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_ARROW, TRUE, SHAULA_TOOL_ARROW,
     GDK_KEY_KP_3, 0, "3"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_RECTANGLE, TRUE, SHAULA_TOOL_RECTANGLE,
     GDK_KEY_r, 0, "R"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_LINE, TRUE, SHAULA_TOOL_LINE, GDK_KEY_4,
     0, "4"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_LINE, TRUE, SHAULA_TOOL_LINE, GDK_KEY_KP_4,
     0, "4"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_LINE, TRUE, SHAULA_TOOL_LINE, GDK_KEY_l, 0,
     "L"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_TEXT, TRUE, SHAULA_TOOL_TEXT, GDK_KEY_5,
     0, "5"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_TEXT, TRUE, SHAULA_TOOL_TEXT,
     GDK_KEY_KP_5, 0, "5"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_TEXT, TRUE, SHAULA_TOOL_TEXT, GDK_KEY_t,
     0, "T"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_PEN, TRUE, SHAULA_TOOL_PEN, GDK_KEY_6, 0,
     "6"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_PEN, TRUE, SHAULA_TOOL_PEN, GDK_KEY_KP_6,
     0, "6"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_PEN, TRUE, SHAULA_TOOL_PEN, GDK_KEY_p, 0,
     "P"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_PEN, TRUE, SHAULA_TOOL_PEN, GDK_KEY_x, 0,
     "X"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_HIGHLIGHT, TRUE, SHAULA_TOOL_HIGHLIGHT,
     GDK_KEY_7, 0, "7"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_HIGHLIGHT, TRUE, SHAULA_TOOL_HIGHLIGHT,
     GDK_KEY_KP_7, 0, "7"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_HIGHLIGHT, TRUE, SHAULA_TOOL_HIGHLIGHT,
     GDK_KEY_h, 0, "H"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_MEASURE, TRUE, SHAULA_TOOL_MEASURE,
     GDK_KEY_8, 0, "8"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_MEASURE, TRUE, SHAULA_TOOL_MEASURE,
     GDK_KEY_KP_8, 0, "8"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_SPOTLIGHT, TRUE, SHAULA_TOOL_SPOTLIGHT,
     GDK_KEY_9, 0, "9"},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_SPOTLIGHT, TRUE, SHAULA_TOOL_SPOTLIGHT,
     GDK_KEY_KP_9, 0, "9"},
    {SHAULA_PREVIEW_COMMAND_RESET_ANNOTATIONS, FALSE, SHAULA_TOOL_SELECT, 0, 0,
     NULL},
    {SHAULA_PREVIEW_COMMAND_CROP_SELECTED, FALSE, SHAULA_TOOL_SELECT, 0, 0,
     NULL},
    {SHAULA_PREVIEW_COMMAND_BLUR_REGION, FALSE, SHAULA_TOOL_SELECT, 0, 0,
     NULL},
    {SHAULA_PREVIEW_COMMAND_ERASE_REGION, FALSE, SHAULA_TOOL_SELECT, 0, 0,
     NULL},
    {SHAULA_PREVIEW_COMMAND_SPOTLIGHT_REGION, FALSE, SHAULA_TOOL_SELECT, 0, 0,
     NULL},
    {SHAULA_PREVIEW_COMMAND_COPY_PATH, FALSE, SHAULA_TOOL_SELECT, 0, 0, NULL},
    {SHAULA_PREVIEW_COMMAND_OPEN_CONTAINING_FOLDER, FALSE, SHAULA_TOOL_SELECT,
     0, 0, NULL},
    {SHAULA_PREVIEW_COMMAND_CLOSE, FALSE, SHAULA_TOOL_SELECT, 0, 0, NULL},
    {SHAULA_PREVIEW_COMMAND_DISCARD, FALSE, SHAULA_TOOL_SELECT, 0, 0, NULL},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_SELECT, TRUE, SHAULA_TOOL_SELECT, 0, 0,
     NULL},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_HAND, TRUE, SHAULA_TOOL_HAND, 0, 0,
     NULL},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_CROP, TRUE, SHAULA_TOOL_CROP, 0, 0,
     NULL},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_ERASER, TRUE, SHAULA_TOOL_ERASER, 0, 0,
     NULL},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_ARROW, TRUE, SHAULA_TOOL_ARROW, 0, 0,
     NULL},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_LINE, TRUE, SHAULA_TOOL_LINE, 0, 0, NULL},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_TEXT, TRUE, SHAULA_TOOL_TEXT, 0, 0, NULL},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_MEASURE, TRUE, SHAULA_TOOL_MEASURE, 0, 0,
     NULL},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_RECTANGLE, TRUE, SHAULA_TOOL_RECTANGLE, 0,
     0, NULL},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_HIGHLIGHT, TRUE, SHAULA_TOOL_HIGHLIGHT, 0,
     0, NULL},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_PEN, TRUE, SHAULA_TOOL_PEN, 0, 0, NULL},
    {SHAULA_PREVIEW_COMMAND_SET_TOOL_SPOTLIGHT, TRUE, SHAULA_TOOL_SPOTLIGHT, 0,
     0, NULL},
};

static const ShaulaPreviewCommandSpec *
find_command_spec(ShaulaPreviewCommand command) {
  for (guint i = 0; i < G_N_ELEMENTS(command_specs); i++) {
    if (command_specs[i].command == command)
      return &command_specs[i];
  }
  return NULL;
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
  case SHAULA_PREVIEW_COMMAND_COPY_SELECTED_ANNOTATION:
  case SHAULA_PREVIEW_COMMAND_CUT_SELECTED_ANNOTATION:
    return shaula_annotation_editor_has_selection(state);
  case SHAULA_PREVIEW_COMMAND_PASTE_ANNOTATION:
    return shaula_annotation_editor_can_paste(state);
  case SHAULA_PREVIEW_COMMAND_DELETE_SELECTED:
    return shaula_annotation_editor_has_selection(state);
  case SHAULA_PREVIEW_COMMAND_SELECT_ALL_ANNOTATIONS:
    return state->operation == SHAULA_OPERATION_NONE &&
           state->document.annotations != NULL &&
           state->document.annotations->len > 0;
  case SHAULA_PREVIEW_COMMAND_CROP_SELECTED:
    if (state->active_tool == SHAULA_TOOL_SELECT &&
        state->has_region_selection)
      return TRUE;
    if (state->active_tool == SHAULA_TOOL_SELECT) {
      ShaulaAnnotation *annotation =
          shaula_annotation_editor_single_selection(state);
      return annotation != NULL &&
             annotation->type == SHAULA_ANNOTATION_RECTANGLE;
    }
    return FALSE;
  case SHAULA_PREVIEW_COMMAND_BLUR_REGION:
  case SHAULA_PREVIEW_COMMAND_ERASE_REGION:
  case SHAULA_PREVIEW_COMMAND_SPOTLIGHT_REGION:
    return state->active_tool == SHAULA_TOOL_SELECT &&
           state->has_region_selection && state->document.image != NULL;
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_CROP:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_ERASER:
    return state->document.image != NULL;
  case SHAULA_PREVIEW_COMMAND_COPY:
    return state->document.image != NULL ||
           shaula_annotation_editor_has_selection(state);
  case SHAULA_PREVIEW_COMMAND_SAVE:
  case SHAULA_PREVIEW_COMMAND_SAVE_AS:
  case SHAULA_PREVIEW_COMMAND_DONE:
  case SHAULA_PREVIEW_COMMAND_FIT_TO_SCREEN:
  case SHAULA_PREVIEW_COMMAND_ACTUAL_SIZE:
  case SHAULA_PREVIEW_COMMAND_ZOOM_IN:
  case SHAULA_PREVIEW_COMMAND_ZOOM_OUT:
    return state->document.image != NULL;
  case SHAULA_PREVIEW_COMMAND_RESET_ANNOTATIONS:
    return state->document.annotations != NULL && state->document.annotations->len > 0;
  case SHAULA_PREVIEW_COMMAND_COPY_PATH:
  case SHAULA_PREVIEW_COMMAND_OPEN_CONTAINING_FOLDER:
    return state->document.path != NULL;
  case SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR:
    return state->document.image != NULL;
  case SHAULA_PREVIEW_COMMAND_USE_HOVER_COLOR:
    return state->hover_color_valid;
  case SHAULA_PREVIEW_COMMAND_CLOSE:
  case SHAULA_PREVIEW_COMMAND_DISCARD:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_SELECT:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_HAND:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_ARROW:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_LINE:
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

  const ShaulaPreviewCommandSpec *spec = find_command_spec(command);
  if (spec != NULL && spec->is_tool_command) {
    shaula_preview_action_set_tool(state, spec->tool);
    return TRUE;
  }

  switch (command) {
  case SHAULA_PREVIEW_COMMAND_COPY:
    if (shaula_annotation_editor_has_selection(state))
      return shaula_annotation_editor_copy_selected(state);
    shaula_preview_action_copy(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_COPY_SELECTED_ANNOTATION:
    return shaula_annotation_editor_copy_selected(state);
  case SHAULA_PREVIEW_COMMAND_CUT_SELECTED_ANNOTATION:
    return shaula_annotation_editor_cut_selected(state);
  case SHAULA_PREVIEW_COMMAND_PASTE_ANNOTATION:
    return shaula_annotation_editor_paste(state);
  case SHAULA_PREVIEW_COMMAND_SAVE:
    shaula_preview_action_save(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_SAVE_AS:
    shaula_preview_action_save_as(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_DONE:
    shaula_preview_action_done(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_UNDO:
    return shaula_preview_undo(state);
  case SHAULA_PREVIEW_COMMAND_REDO:
    return shaula_preview_redo(state);
  case SHAULA_PREVIEW_COMMAND_DUPLICATE_SELECTED:
    return shaula_annotation_editor_duplicate_selected(state);
  case SHAULA_PREVIEW_COMMAND_DELETE_SELECTED:
    shaula_annotation_editor_delete_selected(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_SELECT_ALL_ANNOTATIONS:
    shaula_annotation_editor_select_all(state);
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
    shaula_annotation_editor_reset_annotations(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_COPY_PATH:
    shaula_preview_action_copy_path(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR:
    shaula_preview_action_copy_hover_color(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_USE_HOVER_COLOR:
    shaula_preview_action_use_hover_color(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_OPEN_CONTAINING_FOLDER:
    shaula_preview_action_open_containing_folder(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_CLOSE:
    shaula_preview_action_close(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_DISCARD:
    shaula_preview_action_discard(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_FIT_TO_SCREEN:
    shaula_preview_set_fit_mode(state, TRUE);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_ACTUAL_SIZE:
    shaula_preview_set_actual_size(state);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_ZOOM_IN:
    shaula_preview_zoom_by_factor(state, 1.12);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_ZOOM_OUT:
    shaula_preview_zoom_by_factor(state, 1.0 / 1.12);
    return TRUE;
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_SELECT:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_HAND:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_CROP:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_ERASER:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_ARROW:
  case SHAULA_PREVIEW_COMMAND_SET_TOOL_LINE:
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
  for (guint i = 0; i < G_N_ELEMENTS(command_specs); i++) {
    const ShaulaPreviewCommandSpec *spec = &command_specs[i];
    if (spec->keyval != 0 && spec->keyval == keyval &&
        spec->modifiers == normalized) {
      if (command != NULL)
        *command = spec->command;
      return TRUE;
    }
  }
  return FALSE;
}

gboolean shaula_preview_command_for_tool(ShaulaTool tool,
                                         ShaulaPreviewCommand *command) {
  for (guint i = 0; i < G_N_ELEMENTS(command_specs); i++) {
    const ShaulaPreviewCommandSpec *spec = &command_specs[i];
    if (spec->is_tool_command && spec->tool == tool) {
      if (command != NULL)
        *command = spec->command;
      return TRUE;
    }
  }
  return FALSE;
}

const char *shaula_preview_command_shortcut_label(
    ShaulaPreviewCommand command) {
  const ShaulaPreviewCommandSpec *spec = find_command_spec(command);
  return spec != NULL ? spec->shortcut_label : NULL;
}

const char *shaula_preview_tool_shortcut_hint(ShaulaTool tool) {
  if ((guint)tool >= SHAULA_TOOL_COUNT)
    return NULL;
  return tool_shortcut_hints[tool];
}

const char *shaula_preview_tool_shortcut_badge(ShaulaTool tool) {
  if ((guint)tool >= SHAULA_TOOL_COUNT)
    return NULL;
  return tool_shortcut_badges[tool];
}
