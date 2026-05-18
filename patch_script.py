import re

with open("src/settings/native_gtk_settings.c", "r") as f:
    content = f.read()

# 1. Rename switches in AppState
content = content.replace("GtkSwitch *quick_skip_switch;", "GtkSwitch *quick_open_switch;")
content = content.replace("GtkSwitch *area_skip_switch;", "GtkSwitch *area_open_switch;")
content = content.replace("GtkSwitch *fullscreen_skip_switch;", "GtkSwitch *fullscreen_open_switch;")
content = content.replace("GtkSwitch *all_screens_skip_switch;", "GtkSwitch *all_screens_open_switch;")

# 2. Update read_controls
content = content.replace("config->quick_skip_preview = gtk_switch_get_active(state.quick_skip_switch);", "config->quick_skip_preview = !gtk_switch_get_active(state.quick_open_switch);")
content = content.replace("config->area_skip_preview = gtk_switch_get_active(state.area_skip_switch);", "config->area_skip_preview = !gtk_switch_get_active(state.area_open_switch);")
content = content.replace("config->fullscreen_skip_preview =\n      gtk_switch_get_active(state.fullscreen_skip_switch);", "config->fullscreen_skip_preview = !gtk_switch_get_active(state.fullscreen_open_switch);")
content = content.replace("config->all_screens_skip_preview =\n      gtk_switch_get_active(state.all_screens_skip_switch);", "config->all_screens_skip_preview = !gtk_switch_get_active(state.all_screens_open_switch);")

# 3. Update dynamic controls
content = content.replace("gtk_switch_get_active(state.quick_skip_switch)", "!gtk_switch_get_active(state.quick_open_switch)")
content = content.replace("gtk_switch_get_active(state.area_skip_switch)", "!gtk_switch_get_active(state.area_open_switch)")
content = content.replace("gtk_switch_get_active(state.fullscreen_skip_switch)", "!gtk_switch_get_active(state.fullscreen_open_switch)")
content = content.replace("gtk_switch_get_active(state.all_screens_skip_switch)", "!gtk_switch_get_active(state.all_screens_open_switch)")

with open("src/settings/native_gtk_settings.c", "w") as f:
    f.write(content)
