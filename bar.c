#include "bar.h"
#include "config.h"
#include "monitor.h"
#include "client.h"
#include "layout.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

typedef void (*bar_slot_render_fn)(char *out, size_t out_sz, Monitor *m);

typedef struct {
	const char *name;
	bar_slot_render_fn render;
	int interval_sec;
	time_t last_run;
	char cached[128];
} BarSlot;

static void slot_render_datetime(char *out, size_t out_sz, Monitor *m __attribute__((unused)))
{
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	if (!tm) {
		out[0] = '\0';
		return;
	}
	strftime(out, out_sz, "%Y-%m-%d %H:%M", tm);
}

static BarSlot right_slots[] = {
	{ .name = "datetime", .render = slot_render_datetime, .interval_sec = 10, .last_run = 0, .cached = "" },
};

#define RIGHT_SLOT_COUNT (sizeof(right_slots) / sizeof(right_slots[0]))

static bool bar_slots_update(time_t now)
{
	bool changed = false;

	for (size_t i = 0; i < RIGHT_SLOT_COUNT; i++) {
		BarSlot *slot = &right_slots[i];
		if (!slot->render)
			continue;

		int interval = slot->interval_sec;
		if (interval < 1)
			interval = 1;

		if (slot->last_run != 0 && (now - slot->last_run) < interval)
			continue;

		char next[sizeof(slot->cached)];
		slot->render(next, sizeof(next), monitor_get_focused());
		next[sizeof(next) - 1] = '\0';

		if (strcmp(next, slot->cached) != 0) {
			size_t n = strlen(next);
			if (n >= sizeof(slot->cached))
				n = sizeof(slot->cached) - 1;
			memcpy(slot->cached, next, n);
			slot->cached[n] = '\0';
			changed = true;
		}
		slot->last_run = now;
	}

	return changed;
}

static void build_right_text(char *out, size_t out_sz)
{
	size_t used = 0;
	out[0] = '\0';

	for (size_t i = 0; i < RIGHT_SLOT_COUNT; i++) {
		const char *txt = right_slots[i].cached;
		if (!txt || txt[0] == '\0')
			continue;

		if (used > 0) {
			int n = snprintf(out + used, out_sz - used, " | ");
			if (n < 0 || (size_t)n >= out_sz - used)
				return;
			used += (size_t)n;
		}

		int n = snprintf(out + used, out_sz - used, "%s", txt);
		if (n < 0 || (size_t)n >= out_sz - used)
			return;
		used += (size_t)n;
	}
}

void bar_draw(xcb_connection_t *conn, Monitor *m)
{
	if (m->bar_win == XCB_NONE)
		return;

	bar_slots_update(time(NULL));

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

	/* Build right text from modular slot cache */
	char right[256];
	build_right_text(right, sizeof(right));

	/* Draw left text */
	uint32_t fg = COLOR_BAR_FG;
	xcb_change_gc(conn, m->bar_gc, XCB_GC_FOREGROUND, &fg);

	int left_len = strlen(left);
	if (left_len > 0)
		xcb_image_text_8(conn, left_len, m->bar_win, m->bar_gc,
			4, BAR_HEIGHT - 4, left);

	/* Draw right text (~6px per char with fixed font) */
	int right_len = strlen(right);
	if (right_len > 255)
		right_len = 255;
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

void bar_tick(xcb_connection_t *conn, time_t now)
{
	if (bar_slots_update(now))
		bar_draw_all(conn);
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
