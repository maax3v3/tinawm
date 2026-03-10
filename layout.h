#ifndef LAYOUT_H
#define LAYOUT_H

#include <xcb/xcb.h>

typedef enum {
	LAYOUT_GRID,
	LAYOUT_MONOCLE,
} LayoutMode;

/* Forward declaration - full definition in monitor.h */
struct Monitor;

void layout_apply(xcb_connection_t *conn, struct Monitor *m);

/* Directional focus in grid mode; cyclic focus in monocle mode.
 * dx/dy: -1, 0, or 1 for direction. */
void layout_focus_dir(xcb_connection_t *conn, struct Monitor *m, int dx, int dy);

#endif /* LAYOUT_H */
