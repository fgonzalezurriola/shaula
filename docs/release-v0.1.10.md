# Shaula v0.1.10

This release fixes copying and saving a Preview while text is being edited.
The visible draft is committed before export, including when an annotation is
selected. Copy and save shortcuts work while the text editor has focus.

Development commands now use the standard Wayland clipboard provider instead
of forcing the older GTK provider.

GNOME and KDE interactive capture and shortcut validation remains pending.
