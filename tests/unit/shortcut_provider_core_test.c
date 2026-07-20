#include "shortcuts/provider_core.h"

#include <glib.h>

static void test_action_mapping(void) {
  g_assert_cmpint(shaula_shortcut_action_from_id("quick"), ==,
                  SHAULA_SHORTCUT_ACTION_QUICK);
  g_assert_cmpint(shaula_shortcut_action_from_id("area"), ==,
                  SHAULA_SHORTCUT_ACTION_AREA);
  g_assert_cmpint(shaula_shortcut_action_from_id("fullscreen"), ==,
                  SHAULA_SHORTCUT_ACTION_FULLSCREEN);
  g_assert_cmpint(shaula_shortcut_action_from_id("all_screens"), ==,
                  SHAULA_SHORTCUT_ACTION_ALL_SCREENS);
  g_assert_cmpint(shaula_shortcut_action_from_id("unknown"), ==,
                  SHAULA_SHORTCUT_ACTION_INVALID);

  g_auto(GStrv) quick = shaula_shortcut_action_argv(
      "/usr/bin/shaula", SHAULA_SHORTCUT_ACTION_QUICK);
  g_assert_cmpstr(quick[0], ==, "/usr/bin/shaula");
  g_assert_cmpstr(quick[1], ==, "capture");
  g_assert_cmpstr(quick[2], ==, "quick");
  g_assert_cmpstr(quick[3], ==, "--json");
  g_assert_null(quick[4]);

  g_auto(GStrv) fullscreen = shaula_shortcut_action_argv(
      "/usr/bin/shaula", SHAULA_SHORTCUT_ACTION_FULLSCREEN);
  g_assert_cmpstr(fullscreen[2], ==, "fullscreen");
  g_assert_cmpstr(fullscreen[4], ==, "--save");
  g_assert_null(fullscreen[5]);
}

static void test_duplicate_capture_gate(void) {
  ShaulaShortcutProviderGate gate;
  shaula_shortcut_provider_gate_init(&gate);
  g_assert_true(
      shaula_shortcut_provider_should_dispatch(&gate, "quick", 100U));
  g_assert_false(
      shaula_shortcut_provider_should_dispatch(&gate, "quick", 100U));
  g_assert_false(
      shaula_shortcut_provider_should_dispatch(&gate, "area", 101U));
  shaula_shortcut_provider_capture_finished(&gate);
  g_assert_false(
      shaula_shortcut_provider_should_dispatch(&gate, "quick", 102U));
  shaula_shortcut_provider_deactivated(&gate, "quick");
  g_assert_true(
      shaula_shortcut_provider_should_dispatch(&gate, "quick", 103U));
  shaula_shortcut_provider_capture_finished(&gate);
  shaula_shortcut_provider_deactivated(&gate, "quick");
  g_assert_false(
      shaula_shortcut_provider_should_dispatch(&gate, "unknown", 104U));
  shaula_shortcut_provider_gate_clear(&gate);
}

static void test_reconnect_contract(void) {
  ShaulaShortcutProviderGate gate;
  shaula_shortcut_provider_gate_init(&gate);
  const guint expected[] = {1000U, 2000U, 4000U, 8000U,
                            16000U, 30000U, 30000U};
  for (guint i = 0; i < G_N_ELEMENTS(expected); i++)
    g_assert_cmpuint(shaula_shortcut_provider_next_reconnect_delay_ms(&gate),
                     ==, expected[i]);
  shaula_shortcut_provider_reconnected(&gate);
  g_assert_cmpuint(shaula_shortcut_provider_next_reconnect_delay_ms(&gate), ==,
                   1000U);
  shaula_shortcut_provider_gate_clear(&gate);
}

static void test_duplicate_instance_contract(void) {
  g_assert_cmpint(shaula_shortcut_provider_name_result(1U), ==,
                  SHAULA_PROVIDER_NAME_ACQUIRED);
  g_assert_cmpint(shaula_shortcut_provider_name_result(4U), ==,
                  SHAULA_PROVIDER_NAME_ACQUIRED);
  g_assert_cmpint(shaula_shortcut_provider_name_result(2U), ==,
                  SHAULA_PROVIDER_NAME_ALREADY_RUNNING);
  g_assert_cmpint(shaula_shortcut_provider_name_result(3U), ==,
                  SHAULA_PROVIDER_NAME_ALREADY_RUNNING);
  g_assert_cmpint(shaula_shortcut_provider_name_result(0U), ==,
                  SHAULA_PROVIDER_NAME_FAILED);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/shortcut-provider/action-mapping", test_action_mapping);
  g_test_add_func("/shortcut-provider/duplicate-capture-gate",
                  test_duplicate_capture_gate);
  g_test_add_func("/shortcut-provider/reconnect-contract",
                  test_reconnect_contract);
  g_test_add_func("/shortcut-provider/duplicate-instance-contract",
                  test_duplicate_instance_contract);
  return g_test_run();
}
