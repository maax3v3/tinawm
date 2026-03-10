#include "bar.h"
#include "config.h"
#include "monitor.h"
#include "client.h"
#include "layout.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void bar_draw(xcb_connection_t *conn, Monitor *m)
{
	if (m->bar_win == XCB_NONE)
		return;

	/* Clear bar */
	uint32_t bg = COLOR_BAR_BG;
	xcb_change_gc(conn, m->bar_gc, XCB_GC_FOREGROUND, &bg);
	xcb_rectangle_t rect = { 0, 0, m->w, BAR_HEIGHT };
	xcb_poly_fill_rectangle(conn, m->bar_win, m->bar_gc, 1, &rect);

	/* Build left text */
	const char *mode_str = (m->mode == LAYOUT_GRID) ? "GRID" : "MONOCLE";
	int wcount = client_list_count(m->clients);
	int current = 0;
	if (wcount > 0) {
		current = m->focused_idx + 1;
		if (current < 1)
			current = 1;
		if (current > wcount)
			current = wcount;
	}
	char left[256];
	snprintf(left, sizeof(left), " [WS %d] [%s] [window %d/%d]",
		m->num + 1, mode_str, current, wcount);

	/* Build right text: datetime */
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	char right[64];
	strftime(right, sizeof(right), "%Y-%m-%d %H:%M ", tm);

	/* Draw left text */
	uint32_t fg = COLOR_BAR_FG;
	xcb_change_gc(conn, m->bar_gc, XCB_GC_FOREGROUND, &fg);

	int left_len = strlen(left);
	if (left_len > 0)
		xcb_image_text_8(conn, left_len, m->bar_win, m->bar_gc,
			4, BAR_HEIGHT - 4, left);

	/* Draw right text (~6px per char with fixed font) */
	int right_len = strlen(right);
	int right_x = m->w - (right_len * 6) - 4;
	if (right_x < 0) right_x = 0;
	if (right_len > 0)
		xcb_image_text_8(conn, right_len, m->bar_win, m->bar_gc,
			right_x, BAR_HEIGHT - 4, right);

	/* Ensure bar stays on top */
	uint32_t stack[] = { XCB_STACK_MODE_ABOVE };
	xcb_configure_window(conn, m->bar_win, XCB_CONFIG_WINDOW_STACK_MODE, stack);

	xcb_flush(conn);
}

void bar_draw_all(xcb_connection_t *conn)
{
	for (Monitor *m = monitor_get_list(); m; m = m->next)
		bar_draw(conn, m);
}

bool bar_handle_expose(xcb_connection_t *conn, xcb_expose_event_t *ev)
{
	for (Monitor *m = monitor_get_list(); m; m = m->next) {
		if (ev->window == m->bar_win) {
			bar_draw(conn, m);
			return true;
		}
	}
	return false;
}

bool bar_is_bar_window(xcb_window_t win)
{
	for (Monitor *m = monitor_get_list(); m; m = m->next)
		if (win == m->bar_win)
			return true;
	return false;
}
