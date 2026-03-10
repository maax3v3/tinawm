#ifndef BAR_H
#define BAR_H

#include <stdbool.h>
#include <xcb/xcb.h>

struct Monitor;

void bar_draw(xcb_connection_t *conn, struct Monitor *m);
void bar_draw_all(xcb_connection_t *conn);
bool bar_handle_expose(xcb_connection_t *conn, xcb_expose_event_t *ev);
bool bar_is_bar_window(xcb_window_t win);

#endif /* BAR_H */
