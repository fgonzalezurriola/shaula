#include <glib.h>

#include "overlay_selection_session.h"

static ShaulaOverlaySelectionSession *new_session(void) {
  ShaulaOverlaySelectionSession *session = shaula_overlay_selection_session_new(
      (ShaulaPoint){.x = 800, .y = 600});
  g_assert_nonnull(session);
  return session;
}

static const ShaulaOverlaySelectionView *view(
    ShaulaOverlaySelectionSession *session) {
  const ShaulaOverlaySelectionView *current =
      shaula_overlay_selection_session_view(session);
  g_assert_nonnull(current);
  return current;
}

static void test_create_and_confirm(void) {
  ShaulaOverlaySelectionSession *session = new_session();
  shaula_overlay_selection_session_begin(
      session, (ShaulaPoint){.x = 100, .y = 120}, FALSE);
  g_assert_true(shaula_overlay_selection_session_update(session, 200, 100));
  const ShaulaOverlaySelectionView *current = view(session);
  g_assert_true(current->has_selection);
  g_assert_cmpint(current->selection.x, ==, 100);
  g_assert_cmpint(current->selection.y, ==, 120);
  g_assert_cmpint(current->selection.width, ==, 201);
  g_assert_cmpint(current->selection.height, ==, 101);
  g_assert_true(
      shaula_overlay_selection_session_end(session, 200, 100, TRUE));
  g_assert_cmpint(current->drag_mode, ==, SHAULA_OVERLAY_DRAG_NONE);
  shaula_overlay_selection_session_free(session);
}

static void test_small_create_preserves_selection(void) {
  ShaulaOverlaySelectionSession *session = new_session();
  g_assert_true(shaula_overlay_selection_session_set_selection(
      session, (ShaulaRect){.x = 50, .y = 60, .width = 200, .height = 120},
      TRUE));
  ShaulaRect original = view(session)->selection;
  shaula_overlay_selection_session_begin(
      session, (ShaulaPoint){.x = 400, .y = 400}, FALSE);
  g_assert_false(shaula_overlay_selection_session_update(session, 2, 3));
  g_assert_cmpmem(&view(session)->selection, sizeof(original), &original,
                  sizeof(original));
  shaula_overlay_selection_session_free(session);
}

static void test_move_resize_and_nudge(void) {
  ShaulaOverlaySelectionSession *session = new_session();
  g_assert_true(shaula_overlay_selection_session_set_selection(
      session, (ShaulaRect){.x = 100, .y = 100, .width = 200, .height = 120},
      TRUE));

  shaula_overlay_selection_session_begin(
      session, (ShaulaPoint){.x = 100, .y = 185}, FALSE);
  g_assert_cmpint(view(session)->drag_mode, ==, SHAULA_OVERLAY_DRAG_MOVE);
  g_assert_true(shaula_overlay_selection_session_update(session, 20, 30));
  (void)shaula_overlay_selection_session_end(session, 20, 30, FALSE);
  g_assert_cmpint(view(session)->selection.x, ==, 120);
  g_assert_cmpint(view(session)->selection.y, ==, 130);

  ShaulaRect selection = view(session)->selection;
  shaula_overlay_selection_session_begin(
      session,
      (ShaulaPoint){.x = selection.x + selection.width,
                    .y = selection.y + selection.height},
      FALSE);
  g_assert_cmpint(view(session)->drag_mode, ==, SHAULA_OVERLAY_DRAG_RESIZE);
  g_assert_true(shaula_overlay_selection_session_update(session, 50, 40));
  (void)shaula_overlay_selection_session_end(session, 50, 40, FALSE);
  g_assert_cmpint(view(session)->selection.width, >, 200);
  g_assert_cmpint(view(session)->selection.height, >, 120);

  g_assert_true(shaula_overlay_selection_session_nudge(session, -1, -1, 10));
  g_assert_cmpint(view(session)->selection.x, ==, 110);
  g_assert_cmpint(view(session)->selection.y, ==, 120);
  shaula_overlay_selection_session_free(session);
}

static void test_aspect_and_blocked_drag(void) {
  ShaulaOverlaySelectionSession *session = new_session();
  g_assert_true(shaula_overlay_selection_session_set_selection(
      session, (ShaulaRect){.x = 100, .y = 100, .width = 300, .height = 200},
      TRUE));
  g_assert_true(shaula_overlay_selection_session_set_aspect(
      session, TRUE, (ShaulaAspect){.width = 1, .height = 1}, FALSE));
  g_assert_cmpint(view(session)->selection.width, ==,
                  view(session)->selection.height);

  shaula_overlay_selection_session_begin(
      session, (ShaulaPoint){.x = 10, .y = 10}, TRUE);
  g_assert_cmpint(view(session)->drag_mode, ==, SHAULA_OVERLAY_DRAG_BLOCKED);
  g_assert_false(shaula_overlay_selection_session_update(session, 100, 100));
  g_assert_false(
      shaula_overlay_selection_session_end(session, 100, 100, TRUE));
  shaula_overlay_selection_session_free(session);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/overlay-selection-session/create-confirm",
                  test_create_and_confirm);
  g_test_add_func("/overlay-selection-session/small-create-preserves",
                  test_small_create_preserves_selection);
  g_test_add_func("/overlay-selection-session/move-resize-nudge",
                  test_move_resize_and_nudge);
  g_test_add_func("/overlay-selection-session/aspect-blocked",
                  test_aspect_and_blocked_drag);
  return g_test_run();
}
