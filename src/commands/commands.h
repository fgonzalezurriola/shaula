#ifndef SHAULA_COMMANDS_COMMANDS_H
#define SHAULA_COMMANDS_COMMANDS_H

int shaula_preflight_command_run(int argc, char **argv);
int shaula_capabilities_command_run(int argc, char **argv);
int shaula_errors_command_run(int argc, char **argv);
int shaula_settings_command_run(int argc, char **argv);
int shaula_config_command_run(int argc, char **argv);
int shaula_preview_command_run(int argc, char **argv);
int shaula_directory_command_run(int argc, char **argv);
int shaula_history_command_run(int argc, char **argv);
int shaula_clipboard_command_run(int argc, char **argv);
int shaula_explore_command_run(int argc, char **argv);
int shaula_doctor_command_run(int argc, char **argv);
int shaula_setup_command_run(int argc, char **argv);
int shaula_notify_command_run(int argc, char **argv);

#endif
