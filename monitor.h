#ifndef MONITOR_H
#define MONITOR_H

#include "client.h"
#include "layout.h"

#include <xcb/xcb.h>

typedef struct Monitor {
	int num;
	int16_t x, y;
	uint16_t w, h;
	LayoutMode mode;
	int focused_idx;
	Client *clients;
	xcb_window_t bar_win;
	xcb_gcontext_t bar_gc;
	struct Monitor *next;
} Monitor;

void monitors_init(xcb_connection_t *conn, xcb_screen_t *screen, xcb_font_t font);
void monitors_update(xcb_connection_t *conn, xcb_screen_t *screen, xcb_font_t font);
void monitors_cleanup(xcb_connection_t *conn);

Monitor *monitor_get_list(void);
Monitor *monitor_get_focused(void);
Monitor *monitor_get_by_num(int num);
Monitor *monitor_at(int x, int y);
int monitor_count(void);

void monitor_focus(Monitor *m);

#endif /* MONITOR_H */
