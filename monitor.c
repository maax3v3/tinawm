#include "monitor.h"
#include "config.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/randr.h>

static Monitor *monitors = NULL;
static Monitor *focused_mon = NULL;

static Monitor *monitor_create(int num, int16_t x, int16_t y,
                                uint16_t w, uint16_t h)
{
	Monitor *m = calloc(1, sizeof(Monitor));
	if (!m)
		die("failed to allocate monitor");
	m->num = num;
	m->x = x;
	m->y = y;
	m->w = w;
	m->h = h;
	m->mode = LAYOUT_GRID;
	m->focused_idx = 0;
	m->clients = NULL;
	m->bar_win = XCB_NONE;
	m->bar_gc = XCB_NONE;
	m->next = NULL;
	return m;
}

static void monitor_create_bar(xcb_connection_t *conn, xcb_screen_t *screen,
                                xcb_font_t font, Monitor *m)
{
	m->bar_win = xcb_generate_id(conn);
	uint32_t vals[] = {
		COLOR_BAR_BG,
		1, /* override_redirect */
		XCB_EVENT_MASK_EXPOSURE
	};
	xcb_create_window(conn, XCB_COPY_FROM_PARENT, m->bar_win, screen->root,
		m->x, m->y, m->w, BAR_HEIGHT, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
		XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
		vals);

	m->bar_gc = xcb_generate_id(conn);
	uint32_t gc_vals[] = { COLOR_BAR_FG, COLOR_BAR_BG, font };
	xcb_create_gc(conn, m->bar_gc, m->bar_win,
		XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT,
		gc_vals);

	xcb_map_window(conn, m->bar_win);
	uint32_t stack[] = { XCB_STACK_MODE_ABOVE };
	xcb_configure_window(conn, m->bar_win, XCB_CONFIG_WINDOW_STACK_MODE, stack);
}

static void monitor_destroy_bar(xcb_connection_t *conn, Monitor *m)
{
	if (m->bar_gc != XCB_NONE) {
		xcb_free_gc(conn, m->bar_gc);
		m->bar_gc = XCB_NONE;
	}
	if (m->bar_win != XCB_NONE) {
		xcb_destroy_window(conn, m->bar_win);
		m->bar_win = XCB_NONE;
	}
}

void monitors_init(xcb_connection_t *conn, xcb_screen_t *screen, xcb_font_t font)
{
	xcb_randr_get_screen_resources_current_cookie_t rc =
		xcb_randr_get_screen_resources_current(conn, screen->root);
	xcb_randr_get_screen_resources_current_reply_t *rr =
		xcb_randr_get_screen_resources_current_reply(conn, rc, NULL);

	int num = 0;
	Monitor *tail = NULL;

	if (rr) {
		int nout = xcb_randr_get_screen_resources_current_outputs_length(rr);
		xcb_randr_output_t *outputs =
			xcb_randr_get_screen_resources_current_outputs(rr);

		for (int i = 0; i < nout; i++) {
			xcb_randr_get_output_info_cookie_t oc =
				xcb_randr_get_output_info(conn, outputs[i], rr->config_timestamp);
			xcb_randr_get_output_info_reply_t *oi =
				xcb_randr_get_output_info_reply(conn, oc, NULL);
			if (!oi || oi->connection != XCB_RANDR_CONNECTION_CONNECTED ||
			    oi->crtc == XCB_NONE) {
				free(oi);
				continue;
			}

			xcb_randr_get_crtc_info_cookie_t cc =
				xcb_randr_get_crtc_info(conn, oi->crtc, rr->config_timestamp);
			xcb_randr_get_crtc_info_reply_t *ci =
				xcb_randr_get_crtc_info_reply(conn, cc, NULL);
			free(oi);
			if (!ci)
				continue;

			Monitor *m = monitor_create(num++, ci->x, ci->y, ci->width, ci->height);
			monitor_create_bar(conn, screen, font, m);

			if (!monitors) {
				monitors = m;
				tail = m;
			} else {
				tail->next = m;
				tail = m;
			}
			free(ci);
		}
		free(rr);
	}

	/* Fallback: if no RandR outputs, use the whole screen */
	if (!monitors) {
		Monitor *m = monitor_create(0, 0, 0,
			screen->width_in_pixels, screen->height_in_pixels);
		monitor_create_bar(conn, screen, font, m);
		monitors = m;
	}

	focused_mon = monitors;
}

void monitors_update(xcb_connection_t *conn, xcb_screen_t *screen, xcb_font_t font)
{
	/* Simple approach: destroy all monitors and recreate.
	 * Orphaned clients get moved to the first monitor. */
	Client *orphans = NULL;
	for (Monitor *m = monitors; m; m = m->next) {
		/* Collect clients */
		Client *c = m->clients;
		while (c) {
			Client *next = c->next;
			c->next = orphans;
			orphans = c;
			c = next;
		}
		m->clients = NULL;
	}

	/* Destroy old monitors */
	Monitor *m = monitors;
	while (m) {
		Monitor *next = m->next;
		monitor_destroy_bar(conn, m);
		free(m);
		m = next;
	}
	monitors = NULL;
	focused_mon = NULL;

	monitors_init(conn, screen, font);

	/* Re-assign orphaned clients to appropriate monitors */
	Client *c = orphans;
	while (c) {
		Client *next = c->next;
		Monitor *target = monitor_at(c->x, c->y);
		if (!target)
			target = monitors;
		c->next = target->clients;
		target->clients = c;
		c = next;
	}
}

void monitors_cleanup(xcb_connection_t *conn)
{
	Monitor *m = monitors;
	while (m) {
		Monitor *next = m->next;
		monitor_destroy_bar(conn, m);
		free(m);
		m = next;
	}
	monitors = NULL;
	focused_mon = NULL;
}

Monitor *monitor_get_list(void)
{
	return monitors;
}

Monitor *monitor_get_focused(void)
{
	return focused_mon;
}

Monitor *monitor_get_by_num(int num)
{
	for (Monitor *m = monitors; m; m = m->next)
		if (m->num == num)
			return m;
	return NULL;
}

Monitor *monitor_at(int x, int y)
{
	for (Monitor *m = monitors; m; m = m->next) {
		if (x >= m->x && x < m->x + m->w &&
		    y >= m->y && y < m->y + m->h)
			return m;
	}
	return monitors;
}

int monitor_count(void)
{
	int n = 0;
	for (Monitor *m = monitors; m; m = m->next)
		n++;
	return n;
}

void monitor_focus(Monitor *m)
{
	if (m)
		focused_mon = m;
}
