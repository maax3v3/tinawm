#include "config.h"
#include "util.h"
#include "client.h"
#include "monitor.h"
#include "keys.h"
#include "layout.h"
#include "bar.h"
#include "ewmh.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>

static xcb_connection_t *conn;
static xcb_screen_t *screen;
static xcb_window_t root;
static xcb_font_t font;
static bool running = true;

/* Cached atoms */
static xcb_atom_t atom_wm_protocols;
static xcb_atom_t atom_wm_delete_window;
static xcb_atom_t atom_net_wm_name;
static xcb_atom_t atom_utf8_string;
static void focus_client(Client *c);

#define LAUNCHER_MAX_CMD 256
static bool launcher_visible = false;
static xcb_window_t launcher_win = XCB_NONE;
static xcb_gcontext_t launcher_gc = XCB_NONE;
static xcb_key_symbols_t *launcher_keysyms = NULL;
static int launcher_w = 480;
static int launcher_h = 28;
static char launcher_cmd[LAUNCHER_MAX_CMD];
static int launcher_len = 0;
static xcb_window_t launcher_prev_focus = XCB_NONE;
static xcb_window_t focused_win = XCB_NONE;

static xcb_atom_t intern_atom_helper(const char *name)
{
	xcb_intern_atom_cookie_t c = xcb_intern_atom(conn, 0, strlen(name), name);
	xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(conn, c, NULL);
	xcb_atom_t a = r ? r->atom : XCB_NONE;
	free(r);
	return a;
}

static void atoms_init(void)
{
	atom_wm_protocols = intern_atom_helper("WM_PROTOCOLS");
	atom_wm_delete_window = intern_atom_helper("WM_DELETE_WINDOW");
	atom_net_wm_name = intern_atom_helper("_NET_WM_NAME");
	atom_utf8_string = intern_atom_helper("UTF8_STRING");
}

/* --- Global client search (used by client.h declarations) --- */

Client *client_find(xcb_window_t win)
{
	for (Monitor *m = monitor_get_list(); m; m = m->next)
		for (Client *c = m->clients; c; c = c->next)
			if (c->window == win)
				return c;
	return NULL;
}

Client *client_find_by_frame(xcb_window_t frame)
{
	for (Monitor *m = monitor_get_list(); m; m = m->next)
		for (Client *c = m->clients; c; c = c->next)
			if (c->frame == frame)
				return c;
	return NULL;
}

/* Find which monitor owns a client */
static Monitor *client_to_monitor(Client *target)
{
	for (Monitor *m = monitor_get_list(); m; m = m->next)
		for (Client *c = m->clients; c; c = c->next)
			if (c == target)
				return m;
	return NULL;
}

/* --- WM actions called from keys.c --- */

void wm_quit(void)
{
	running = false;
}

static void relayout_monitor(Monitor *m)
{
	layout_apply(conn, m);
	bar_draw(conn, m);
}

static void relayout_all(void)
{
	for (Monitor *m = monitor_get_list(); m; m = m->next)
		relayout_monitor(m);
}

static void monitor_set_focused_client(Monitor *m, Client *target)
{
	if (!m || !target)
		return;
	int i = 0;
	for (Client *c = m->clients; c; c = c->next, i++) {
		if (c == target) {
			m->focused_idx = i;
			return;
		}
	}
}

static Client *monitor_client_at(Monitor *m, int idx)
{
	if (!m || idx < 0)
		return NULL;
	int i = 0;
	for (Client *c = m->clients; c; c = c->next, i++) {
		if (i == idx)
			return c;
	}
	return NULL;
}

static int monitor_client_index(Monitor *m, Client *target)
{
	if (!m || !target)
		return -1;
	int i = 0;
	for (Client *c = m->clients; c; c = c->next, i++) {
		if (c == target)
			return i;
	}
	return -1;
}

/* Resolve an arbitrary focused X window to a managed client (client or frame). */
static Client *resolve_managed_client(xcb_window_t win)
{
	if (win == XCB_NONE || win == root)
		return NULL;

	Client *c = client_find(win);
	if (c)
		return c;
	c = client_find_by_frame(win);
	if (c)
		return c;

	xcb_window_t current = win;
	for (int depth = 0; depth < 32 && current != XCB_NONE && current != root; depth++) {
		xcb_query_tree_cookie_t qc = xcb_query_tree(conn, current);
		xcb_query_tree_reply_t *qr = xcb_query_tree_reply(conn, qc, NULL);
		if (!qr)
			break;
		xcb_window_t parent = qr->parent;
		free(qr);
		if (parent == XCB_NONE || parent == current)
			break;

		c = client_find(parent);
		if (c)
			return c;
		c = client_find_by_frame(parent);
		if (c)
			return c;

		current = parent;
	}

	return NULL;
}

static void launcher_draw(void)
{
	if (launcher_win == XCB_NONE || launcher_gc == XCB_NONE)
		return;

	uint32_t bg = COLOR_BAR_BG;
	xcb_change_gc(conn, launcher_gc, XCB_GC_FOREGROUND, &bg);
	xcb_rectangle_t rect = { 0, 0, launcher_w, launcher_h };
	xcb_poly_fill_rectangle(conn, launcher_win, launcher_gc, 1, &rect);

	uint32_t fg = COLOR_BAR_FG;
	xcb_change_gc(conn, launcher_gc, XCB_GC_FOREGROUND, &fg);

	char line[LAUNCHER_MAX_CMD + 16];
	snprintf(line, sizeof(line), "run: %s_", launcher_cmd);
	int len = (int)strlen(line);
	if (len > 250) {
		len = 250;
		line[len] = '\0';
	}
	if (len > 0)
		xcb_image_text_8(conn, len, launcher_win, launcher_gc, 8, launcher_h - 8, line);
}

static void launcher_close(bool run_cmd)
{
	if (!launcher_visible)
		return;

	xcb_ungrab_keyboard(conn, XCB_CURRENT_TIME);
	xcb_unmap_window(conn, launcher_win);
	launcher_visible = false;

	if (run_cmd && launcher_len > 0)
		spawn(launcher_cmd);

	Client *restore = client_find(launcher_prev_focus);
	if (restore) {
		focus_client(restore);
	} else {
		focused_win = XCB_NONE;
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		ewmh_update_active_window(conn, root, XCB_NONE);
		bar_draw_all(conn);
	}

	xcb_flush(conn);
}

static void launcher_handle_keypress(xcb_key_press_event_t *ev)
{
	if (!launcher_visible)
		return;

	int col = (ev->state & XCB_MOD_MASK_SHIFT) ? 1 : 0;
	xcb_keysym_t sym = xcb_key_press_lookup_keysym(launcher_keysyms, ev, col);

	if (sym == XK_Escape) {
		launcher_close(false);
		return;
	}
	if (sym == XK_Return || sym == XK_KP_Enter) {
		launcher_close(true);
		return;
	}
	if (sym == XK_BackSpace) {
		if (launcher_len > 0) {
			launcher_len--;
			launcher_cmd[launcher_len] = '\0';
		}
		launcher_draw();
		xcb_flush(conn);
		return;
	}
	if (sym >= XK_space && sym <= XK_asciitilde && launcher_len < LAUNCHER_MAX_CMD - 1) {
		launcher_cmd[launcher_len++] = (char)sym;
		launcher_cmd[launcher_len] = '\0';
		launcher_draw();
		xcb_flush(conn);
	}
}

static void launcher_open(void)
{
	if (launcher_visible || launcher_win == XCB_NONE)
		return;

	Monitor *m = monitor_get_focused();
	int mon_x = m ? m->x : 0;
	int mon_y = m ? m->y : 0;
	int mon_w = m ? m->w : screen->width_in_pixels;
	int mon_h = m ? m->h : screen->height_in_pixels;

	launcher_w = mon_w * 3 / 5;
	if (launcher_w < 320)
		launcher_w = 320;
	if (launcher_w > 720)
		launcher_w = 720;
	if (launcher_w > mon_w - 20)
		launcher_w = mon_w - 20;
	if (launcher_w < 200)
		launcher_w = 200;
	launcher_h = BAR_HEIGHT + 8;

	int x = mon_x + (mon_w - launcher_w) / 2;
	int y = mon_y + (mon_h - launcher_h) / 2;
	if (x < 0) x = 0;
	if (y < 0) y = 0;

	uint32_t cvals[] = { (uint32_t)x, (uint32_t)y, (uint32_t)launcher_w, (uint32_t)launcher_h };
	xcb_configure_window(conn, launcher_win,
		XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, cvals);

	uint32_t border = COLOR_BORDER_FOCUSED;
	xcb_change_window_attributes(conn, launcher_win, XCB_CW_BORDER_PIXEL, &border);

	uint32_t stack[] = { XCB_STACK_MODE_ABOVE };
	xcb_configure_window(conn, launcher_win, XCB_CONFIG_WINDOW_STACK_MODE, stack);

	launcher_len = 0;
	launcher_cmd[0] = '\0';
	launcher_prev_focus = focused_win;
	launcher_visible = true;

	xcb_map_window(conn, launcher_win);
	xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, launcher_win, XCB_CURRENT_TIME);
	xcb_grab_keyboard(conn, 1, root, XCB_CURRENT_TIME,
		XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
	launcher_draw();
	xcb_flush(conn);
}

static void launcher_init(void)
{
	launcher_keysyms = xcb_key_symbols_alloc(conn);
	if (!launcher_keysyms)
		die("failed to allocate launcher key symbols");

	launcher_win = xcb_generate_id(conn);
	uint32_t vals[] = {
		COLOR_BAR_BG,
		COLOR_BORDER_FOCUSED,
		1,
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS
	};
	xcb_create_window(conn, XCB_COPY_FROM_PARENT, launcher_win, root,
		0, 0, launcher_w, launcher_h, 1,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
		XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL |
		XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, vals);

	launcher_gc = xcb_generate_id(conn);
	uint32_t gc_vals[] = { COLOR_BAR_FG, COLOR_BAR_BG, font };
	xcb_create_gc(conn, launcher_gc, launcher_win,
		XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT, gc_vals);
}

static void launcher_cleanup(void)
{
	if (launcher_visible)
		launcher_close(false);
	if (launcher_gc != XCB_NONE) {
		xcb_free_gc(conn, launcher_gc);
		launcher_gc = XCB_NONE;
	}
	if (launcher_win != XCB_NONE) {
		xcb_destroy_window(conn, launcher_win);
		launcher_win = XCB_NONE;
	}
	if (launcher_keysyms) {
		xcb_key_symbols_free(launcher_keysyms);
		launcher_keysyms = NULL;
	}
}

void wm_toggle_layout(void)
{
	Monitor *m = monitor_get_focused();
	if (!m) return;
	m->mode = (m->mode == LAYOUT_GRID) ? LAYOUT_MONOCLE : LAYOUT_GRID;
	relayout_monitor(m);
}

void wm_open_launcher(void)
{
	launcher_open();
}

void wm_focus_dir(int dx, int dy)
{
	Monitor *m = monitor_get_focused();
	if (!m) return;
	layout_focus_dir(conn, m, dx, dy);
	if (m->mode == LAYOUT_MONOCLE)
		relayout_monitor(m);
	Client *c = monitor_client_at(m, m->focused_idx);
	if (c)
		focus_client(c);
	xcb_flush(conn);
}

void wm_focus_monitor(int num)
{
	Monitor *m = monitor_get_by_num(num);
	if (!m) return;
	monitor_focus(m);
	/* Keep selected client index on target monitor when possible. */
	Client *c = monitor_client_at(m, m->focused_idx);
	if (!c)
		c = m->clients;
	if (c)
		focus_client(c);
	bar_draw_all(conn);
	xcb_flush(conn);
}

/* --- Title bar and frame helpers --- */

static void client_update_title(Client *c)
{
	xcb_get_property_cookie_t nc = xcb_get_property(conn, 0, c->window,
		atom_net_wm_name, atom_utf8_string, 0, CLIENT_TITLE_MAX / 4);
	xcb_get_property_reply_t *nr = xcb_get_property_reply(conn, nc, NULL);
	if (nr && xcb_get_property_value_length(nr) > 0) {
		int len = xcb_get_property_value_length(nr);
		if (len >= CLIENT_TITLE_MAX) len = CLIENT_TITLE_MAX - 1;
		memcpy(c->title, xcb_get_property_value(nr), len);
		c->title[len] = '\0';
		free(nr);
		return;
	}
	free(nr);

	xcb_get_property_cookie_t wc = xcb_get_property(conn, 0, c->window,
		XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, CLIENT_TITLE_MAX / 4);
	xcb_get_property_reply_t *wr = xcb_get_property_reply(conn, wc, NULL);
	if (wr && xcb_get_property_value_length(wr) > 0) {
		int len = xcb_get_property_value_length(wr);
		if (len >= CLIENT_TITLE_MAX) len = CLIENT_TITLE_MAX - 1;
		memcpy(c->title, xcb_get_property_value(wr), len);
		c->title[len] = '\0';
		free(wr);
		return;
	}
	free(wr);

	snprintf(c->title, CLIENT_TITLE_MAX, "untitled");
}

static void draw_titlebar(Client *c, bool focused);

static void update_frame_border(Client *c, bool focused)
{
	if (!c->frame)
		return;
	uint32_t color = focused ? COLOR_BORDER_FOCUSED : COLOR_BORDER_UNFOCUSED;
	xcb_change_window_attributes(conn, c->frame, XCB_CW_BACK_PIXEL, &color);
	xcb_clear_area(conn, 0, c->frame, 0, 0, 0, 0);
	draw_titlebar(c, focused);
}

static void focus_client(Client *c)
{
	Monitor *m = client_to_monitor(c);
	if (m) {
		monitor_focus(m);
		monitor_set_focused_client(m, c);
	}

	/* Unfocus previous */
	if (focused_win != XCB_NONE && focused_win != c->window) {
		Client *old = client_find(focused_win);
		if (old)
			update_frame_border(old, false);
	}
	focused_win = c->window;
	update_frame_border(c, true);
	xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT,
		c->window, XCB_CURRENT_TIME);
	ewmh_update_active_window(conn, root, c->window);
	bar_draw_all(conn);
}

static void draw_titlebar(Client *c, bool focused)
{
	if (!c->frame || !c->gc)
		return;

	xcb_rectangle_t rect = { 0, 0, c->width + 2 * BORDER_WIDTH, TITLEBAR_HEIGHT };
	uint32_t fg;

	fg = focused ? COLOR_BORDER_FOCUSED : COLOR_BORDER_UNFOCUSED;
	xcb_change_gc(conn, c->gc, XCB_GC_FOREGROUND, &fg);
	xcb_poly_fill_rectangle(conn, c->frame, c->gc, 1, &rect);

	fg = COLOR_TITLEBAR_FG;
	xcb_change_gc(conn, c->gc, XCB_GC_FOREGROUND, &fg);

	int len = strlen(c->title);
	if (len > 0)
		xcb_image_text_8(conn, len, c->frame, c->gc,
			4, TITLEBAR_HEIGHT - 4, c->title);
}

static void frame_client(Client *c)
{
	uint16_t frame_w = c->width + 2 * BORDER_WIDTH;
	uint16_t frame_h = c->height + TITLEBAR_HEIGHT + BORDER_WIDTH;

	c->frame = xcb_generate_id(conn);
	uint32_t vals[] = {
		COLOR_BORDER_UNFOCUSED,
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_ENTER_WINDOW |
		XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
	};
	xcb_create_window(conn, XCB_COPY_FROM_PARENT, c->frame, root,
		c->x, c->y, frame_w, frame_h, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
		XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, vals);

	c->gc = xcb_generate_id(conn);
	uint32_t gc_vals[] = { COLOR_TITLEBAR_FG, COLOR_TITLEBAR_BG, font };
	xcb_create_gc(conn, c->gc, c->frame,
		XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT,
		gc_vals);

	xcb_reparent_window(conn, c->window, c->frame,
		BORDER_WIDTH, TITLEBAR_HEIGHT);

	uint32_t bw[] = { 0 };
	xcb_configure_window(conn, c->window,
		XCB_CONFIG_WINDOW_BORDER_WIDTH, bw);

	xcb_map_window(conn, c->frame);
	xcb_map_window(conn, c->window);
}

static void unframe_client(Client *c)
{
	if (!c->frame)
		return;
	xcb_reparent_window(conn, c->window, root, c->x, c->y);
	xcb_free_gc(conn, c->gc);
	xcb_destroy_window(conn, c->frame);
	c->frame = XCB_NONE;
	c->gc = XCB_NONE;
}

static void remove_client_and_refocus(Client *c)
{
	Monitor *m = client_to_monitor(c);
	if (!m)
		return;

	int removed_idx = monitor_client_index(m, c);
	int new_focus_idx = m->focused_idx;
	xcb_window_t removed_win = c->window;
	bool removed_was_focused = (focused_win == removed_win);

	unframe_client(c);
	client_list_remove(&m->clients, removed_win);

	int n = client_list_count(m->clients);
	if (n == 0) {
		m->focused_idx = 0;
	} else {
		if (new_focus_idx < 0)
			new_focus_idx = 0;
		if (removed_idx >= 0 && new_focus_idx > removed_idx)
			new_focus_idx--;
		if (new_focus_idx >= n)
			new_focus_idx = n - 1;
		m->focused_idx = new_focus_idx;
	}

	if (removed_was_focused) {
		if (n > 0) {
			Client *next = monitor_client_at(m, m->focused_idx);
			if (!next) {
				m->focused_idx = 0;
				next = m->clients;
			}
			if (next)
				focus_client(next);
		} else {
			focused_win = XCB_NONE;
			xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT,
				root, XCB_CURRENT_TIME);
			ewmh_update_active_window(conn, root, XCB_NONE);
		}
	}

	ewmh_update_client_list(conn, root);
	relayout_monitor(m);
}

void wm_close_focused(void)
{
	xcb_window_t focus;
	xcb_get_input_focus_cookie_t fc = xcb_get_input_focus(conn);
	xcb_get_input_focus_reply_t *fr = xcb_get_input_focus_reply(conn, fc, NULL);
	if (!fr)
		return;
	focus = fr->focus;
	free(fr);

	Client *target = resolve_managed_client(focus);
	if (!target)
		return;
	focus = target->window;

	bool supports_delete = false;
	xcb_get_property_cookie_t gpc = xcb_get_property(conn, 0, focus,
		atom_wm_protocols, XCB_ATOM_ATOM, 0, 32);
	xcb_get_property_reply_t *gpr = xcb_get_property_reply(conn, gpc, NULL);
	if (gpr) {
		xcb_atom_t *atoms = (xcb_atom_t *)xcb_get_property_value(gpr);
		int n = xcb_get_property_value_length(gpr) / (int)sizeof(xcb_atom_t);
		for (int i = 0; i < n; i++) {
			if (atoms[i] == atom_wm_delete_window) {
				supports_delete = true;
				break;
			}
		}
		free(gpr);
	}

	if (supports_delete) {
		xcb_client_message_event_t ev = {0};
		ev.response_type = XCB_CLIENT_MESSAGE;
		ev.window = focus;
		ev.type = atom_wm_protocols;
		ev.format = 32;
		ev.data.data32[0] = atom_wm_delete_window;
		ev.data.data32[1] = XCB_CURRENT_TIME;
		xcb_send_event(conn, 0, focus, XCB_EVENT_MASK_NO_EVENT,
			(const char *)&ev);
	} else {
		xcb_kill_client(conn, focus);
	}
	xcb_flush(conn);
}

/* --- Event handlers --- */

static void handle_map_request(xcb_map_request_event_t *ev)
{
	if (bar_is_bar_window(ev->window))
		return;
	if (client_find(ev->window))
		return;

	Client *c = client_create(ev->window);
	if (!c)
		return;

	xcb_get_geometry_cookie_t gc = xcb_get_geometry(conn, ev->window);
	xcb_get_geometry_reply_t *geo = xcb_get_geometry_reply(conn, gc, NULL);
	if (geo) {
		c->x = geo->x;
		c->y = geo->y;
		c->width = geo->width;
		c->height = geo->height;
		free(geo);
	}

	uint32_t vals[] = { XCB_EVENT_MASK_PROPERTY_CHANGE |
	                    XCB_EVENT_MASK_STRUCTURE_NOTIFY };
	xcb_change_window_attributes(conn, ev->window,
		XCB_CW_EVENT_MASK, vals);

	client_update_title(c);
	frame_client(c);
	c->mapped = true;

	/* Add to focused monitor */
	Monitor *m = monitor_get_focused();
	client_list_add(&m->clients, c);

	focus_client(c);

	ewmh_update_client_list(conn, root);
	relayout_monitor(m);
	xcb_flush(conn);
}

static void handle_configure_request(xcb_configure_request_event_t *ev)
{
	Client *c = client_find(ev->window);
	if (!c) {
		uint32_t vals[7];
		uint16_t mask = 0;
		int i = 0;
		if (ev->value_mask & XCB_CONFIG_WINDOW_X)
			{ mask |= XCB_CONFIG_WINDOW_X; vals[i++] = ev->x; }
		if (ev->value_mask & XCB_CONFIG_WINDOW_Y)
			{ mask |= XCB_CONFIG_WINDOW_Y; vals[i++] = ev->y; }
		if (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH)
			{ mask |= XCB_CONFIG_WINDOW_WIDTH; vals[i++] = ev->width; }
		if (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT)
			{ mask |= XCB_CONFIG_WINDOW_HEIGHT; vals[i++] = ev->height; }
		if (ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
			{ mask |= XCB_CONFIG_WINDOW_BORDER_WIDTH; vals[i++] = ev->border_width; }
		if (ev->value_mask & XCB_CONFIG_WINDOW_SIBLING)
			{ mask |= XCB_CONFIG_WINDOW_SIBLING; vals[i++] = ev->sibling; }
		if (ev->value_mask & XCB_CONFIG_WINDOW_STACK_MODE)
			{ mask |= XCB_CONFIG_WINDOW_STACK_MODE; vals[i++] = ev->stack_mode; }
		xcb_configure_window(conn, ev->window, mask, vals);
		xcb_flush(conn);
		return;
	}

	xcb_configure_notify_event_t notify = {0};
	notify.response_type = XCB_CONFIGURE_NOTIFY;
	notify.event = c->window;
	notify.window = c->window;
	notify.x = c->x + BORDER_WIDTH;
	notify.y = c->y + TITLEBAR_HEIGHT;
	notify.width = c->width;
	notify.height = c->height;
	notify.border_width = 0;
	notify.above_sibling = XCB_NONE;
	notify.override_redirect = 0;
	xcb_send_event(conn, 0, c->window, XCB_EVENT_MASK_STRUCTURE_NOTIFY,
		(const char *)&notify);
	xcb_flush(conn);
}

static void handle_destroy_notify(xcb_destroy_notify_event_t *ev)
{
	Client *c = client_find(ev->window);
	if (!c)
		return;
	remove_client_and_refocus(c);
}

static void handle_unmap_notify(xcb_unmap_notify_event_t *ev)
{
	Client *c = client_find(ev->window);
	if (!c)
		return;
	remove_client_and_refocus(c);
}

static void handle_enter_notify(xcb_enter_notify_event_t *ev)
{
	if (launcher_visible)
		return;

	/* Ignore synthetic/internal crossing events to avoid focus flips. */
	if (ev->mode != XCB_NOTIFY_MODE_NORMAL ||
	    ev->detail == XCB_NOTIFY_DETAIL_INFERIOR)
		return;

	if (ev->event == root)
		return;

	Client *c = client_find_by_frame(ev->event);
	if (!c)
		c = client_find(ev->event);
	if (c) {
		focus_client(c);
	}
	xcb_flush(conn);
}

static void handle_expose(xcb_expose_event_t *ev)
{
	if (ev->count != 0)
		return;

	if (launcher_visible && ev->window == launcher_win) {
		launcher_draw();
		xcb_flush(conn);
		return;
	}

	if (bar_handle_expose(conn, ev))
		return;

	Client *c = client_find_by_frame(ev->window);
	if (c)
		draw_titlebar(c, focused_win == c->window);

	xcb_flush(conn);
}

static void handle_property_notify(xcb_property_notify_event_t *ev)
{
	Client *c = client_find(ev->window);
	if (!c)
		return;

	if (ev->atom == atom_net_wm_name || ev->atom == XCB_ATOM_WM_NAME) {
		client_update_title(c);
		draw_titlebar(c, focused_win == c->window);
		xcb_flush(conn);
	}
}

static void run(void)
{
	xcb_generic_event_t *ev;
	while (running && (ev = xcb_wait_for_event(conn))) {
		switch (ev->response_type & ~0x80) {
		case XCB_MAP_REQUEST:
			handle_map_request((xcb_map_request_event_t *)ev);
			break;
		case XCB_CONFIGURE_REQUEST:
			handle_configure_request((xcb_configure_request_event_t *)ev);
			break;
		case XCB_DESTROY_NOTIFY:
			handle_destroy_notify((xcb_destroy_notify_event_t *)ev);
			break;
		case XCB_UNMAP_NOTIFY:
			handle_unmap_notify((xcb_unmap_notify_event_t *)ev);
			break;
		case XCB_ENTER_NOTIFY:
			handle_enter_notify((xcb_enter_notify_event_t *)ev);
			break;
		case XCB_EXPOSE:
			handle_expose((xcb_expose_event_t *)ev);
			break;
		case XCB_PROPERTY_NOTIFY:
			handle_property_notify((xcb_property_notify_event_t *)ev);
			break;
		case XCB_KEY_PRESS:
			if (launcher_visible)
				launcher_handle_keypress((xcb_key_press_event_t *)ev);
			else
				keys_handle(conn, (xcb_key_press_event_t *)ev);
			break;
		default:
			break;
		}
		free(ev);
	}
}

static void setup(void)
{
	conn = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(conn))
		die("cannot open display");

	screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	root = screen->root;

	uint32_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
	                XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
	                XCB_EVENT_MASK_STRUCTURE_NOTIFY |
	                XCB_EVENT_MASK_ENTER_WINDOW |
	                XCB_EVENT_MASK_PROPERTY_CHANGE;
	xcb_void_cookie_t cookie = xcb_change_window_attributes_checked(conn,
		root, XCB_CW_EVENT_MASK, &mask);
	xcb_generic_error_t *err = xcb_request_check(conn, cookie);
	if (err) {
		free(err);
		die("another window manager is already running");
	}

	atoms_init();
	ewmh_init(conn, root);

	font = xcb_generate_id(conn);
	xcb_open_font(conn, font, strlen(FONT_NAME), FONT_NAME);

	monitors_init(conn, screen, font);
	ewmh_update_desktops(conn, root, monitor_count(), 0);

	keys_init(conn, root);
	launcher_init();
	xcb_flush(conn);
}

static void cleanup(void)
{
	/* Unframe all clients on all monitors */
	for (Monitor *m = monitor_get_list(); m; m = m->next) {
		Client *c = m->clients;
		while (c) {
			Client *next = c->next;
			unframe_client(c);
			c = next;
		}
	}

	launcher_cleanup();
	monitors_cleanup(conn);
	xcb_close_font(conn, font);
	keys_cleanup();
	xcb_flush(conn);
	if (conn)
		xcb_disconnect(conn);
}

int main(void)
{
	signal(SIGCHLD, SIG_IGN);
	setup();
	relayout_all();
	run();
	cleanup();
	return 0;
}
