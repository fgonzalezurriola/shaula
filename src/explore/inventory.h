#ifndef SHAULA_EXPLORE_INVENTORY_H
#define SHAULA_EXPLORE_INVENTORY_H

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char *result_json;
  gboolean inventory_available;
} ShaulaExploreInventory;

void shaula_explore_inventory_init(ShaulaExploreInventory *inventory);
void shaula_explore_inventory_clear(ShaulaExploreInventory *inventory);

/*
 * Builds the normalized read-only desktop inventory used by `shaula explore`.
 * Niri process or JSON failures are advisory: an empty inventory is returned
 * with inventory_available=false so callers can emit the stable warning.
 */
gboolean shaula_explore_inventory_build(const char *compositor_kind,
                                         const char *compositor_label,
                                         const char *focused_output_name,
                                         gboolean brief,
                                         ShaulaExploreInventory *inventory);

#ifdef __cplusplus
}
#endif

#endif
