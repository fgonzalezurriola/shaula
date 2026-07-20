#include "settings_shortcuts.h"

#include <glib.h>

static void test_state_text_and_warnings(void) {
  g_assert_cmpstr(shaula_settings_shortcut_state_text(
                      SHAULA_SHORTCUT_STATE_ACTIVE),
                  ==, "Enabled and active");
  g_assert_cmpstr(shaula_settings_shortcut_state_text(
                      SHAULA_SHORTCUT_STATE_PERMISSION_DENIED),
                  ==, "Desktop permission denied");
  g_assert_cmpstr(shaula_settings_shortcut_state_text(
                      SHAULA_SHORTCUT_STATE_RECONNECTING),
                  ==, "Reconnecting");
  g_assert_false(shaula_settings_shortcut_state_is_warning(
      SHAULA_SHORTCUT_STATE_ACTIVE));
  g_assert_true(shaula_settings_shortcut_state_is_warning(
      SHAULA_SHORTCUT_STATE_PERMISSION_DENIED));
  g_assert_true(shaula_settings_shortcut_state_is_warning(
      SHAULA_SHORTCUT_STATE_CONFIG_INVALID));
}

static void test_backend_and_registration_text(void) {
  ShaulaShortcutStatus status;
  shaula_shortcut_status_init(&status);
  status.backend = SHAULA_SHORTCUT_BACKEND_PORTAL;
  status.autostart_installed = TRUE;
  status.provider_running = TRUE;
  g_assert_cmpstr(shaula_settings_shortcut_backend_text(status.backend), ==,
                  "Desktop shortcut service");
  g_assert_cmpstr(shaula_settings_shortcut_registration_text(&status), ==,
                  "Registered and running");
  status.provider_running = FALSE;
  g_assert_cmpstr(shaula_settings_shortcut_registration_text(&status), ==,
                  "Registered; provider not running");
  shaula_shortcut_status_clear(&status);
}

static void test_action_sensitivity(void) {
  ShaulaShortcutStatus status;
  shaula_shortcut_status_init(&status);
  g_assert_true(shaula_settings_shortcut_can_enable(&status));
  g_assert_true(shaula_settings_shortcut_can_disable(&status));
  g_assert_false(shaula_settings_shortcut_can_repair(&status));

  status.setup_completed = TRUE;
  status.enabled_requested = TRUE;
  status.state = SHAULA_SHORTCUT_STATE_RECONNECTING;
  g_assert_true(shaula_settings_shortcut_can_enable(&status));
  g_assert_true(shaula_settings_shortcut_can_disable(&status));
  g_assert_true(shaula_settings_shortcut_can_repair(&status));

  status.state = SHAULA_SHORTCUT_STATE_ACTIVE;
  g_assert_false(shaula_settings_shortcut_can_enable(&status));
  g_assert_true(shaula_settings_shortcut_can_disable(&status));
  shaula_shortcut_status_clear(&status);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/settings/shortcuts/state-text-warnings",
                  test_state_text_and_warnings);
  g_test_add_func("/settings/shortcuts/backend-registration",
                  test_backend_and_registration_text);
  g_test_add_func("/settings/shortcuts/action-sensitivity",
                  test_action_sensitivity);
  return g_test_run();
}
