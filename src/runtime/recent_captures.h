#ifndef SHAULA_RUNTIME_RECENT_CAPTURES_H
#define SHAULA_RUNTIME_RECENT_CAPTURES_H

#include <glib.h>

G_BEGIN_DECLS

/* The recent-capture cache contains only Shaula-created encoded PNGs. Paths
 * returned by list are private cache paths and remain owned by the caller.
 */
gboolean shaula_recent_captures_register(const char *source, GError **error);
GPtrArray *shaula_recent_captures_list(GError **error);

G_END_DECLS

#endif
