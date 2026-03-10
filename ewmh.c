#include "ewmh.h"
#include "client.h"
#include "monitor.h"

#include <stdlib.h>
#include <string.h>

static xcb_atom_t atom_net_supported;
static xcb_atom_t atom_net_client_list;
static xcb_atom_t atom_net_active_window;
static xcb_atom_t atom_net_number_of_desktops;
static xcb_atom_t atom_net_current_desktop;
static xcb_atom_t atom_net_wm_name;
static xcb_atom_t atom_net_wm_state;
static xcb_atom_t atom_net_wm_state_fs;
static xcb_atom_t atom_net_wm_window_type;
static xcb_atom_t atom_net_wm_window_type_dialog;
static xcb_atom_t atom_net_supporting_wm_check;

static xcb_window_t wm_check_win = XCB_NONE;

static xcb_atom_t intern(xcb_connection_t *conn, const char *name)
{
	xcb_intern_atom_cookie_t c = xcb_intern_atom(conn, 0, strlen(name), name);
	xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(conn, c, NULL);
	xcb_atom_t a = r ? r->atom : XCB_NONE;
	free(r);
	return a;
}

void ewmh_init(xcb_connection_t *conn, xcb_window_t root)
{
	atom_net_supported = intern(conn, "_NET_SUPPORTED");
	atom_net_client_list = intern(conn, "_NET_CLIENT_LIST");
	atom_net_active_window = intern(conn, "_NET_ACTIVE_WINDOW");
	atom_net_number_of_desktops = intern(conn, "_NET_NUMBER_OF_DESKTOPS");
	atom_net_current_desktop = intern(conn, "_NET_CURRENT_DESKTOP");
	atom_net_wm_name = intern(conn, "_NET_WM_NAME");
	atom_net_wm_state = intern(conn, "_NET_WM_STATE");
	atom_net_wm_state_fs = intern(conn, "_NET_WM_STATE_FULLSCREEN");
	atom_net_wm_window_type = intern(conn, "_NET_WM_WINDOW_TYPE");
	atom_net_wm_window_type_dialog = intern(conn, "_NET_WM_WINDOW_TYPE_DIALOG");
	atom_net_supporting_wm_check = intern(conn, "_NET_SUPPORTING_WM_CHECK");

	xcb_atom_t supported[] = {
		atom_net_supported,
		atom_net_client_list,
		atom_net_active_window,
		atom_net_number_of_desktops,
		atom_net_current_desktop,
		atom_net_wm_name,
		atom_net_wm_state,
		atom_net_wm_state_fs,
		atom_net_wm_window_type,
		atom_net_wm_window_type_dialog,
		atom_net_supporting_wm_check,
	};

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
		atom_net_supported, XCB_ATOM_ATOM, 32,
		sizeof(supported) / sizeof(supported[0]), supported);

	/* Create a small child window for _NET_SUPPORTING_WM_CHECK */
	wm_check_win = xcb_generate_id(conn);
	xcb_create_window(conn, XCB_COPY_FROM_PARENT, wm_check_win, root,
		-1, -1, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
		XCB_COPY_FROM_PARENT, 0, NULL);

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
		atom_net_supporting_wm_check, XCB_ATOM_WINDOW, 32, 1, &wm_check_win);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, wm_check_win,
		atom_net_supporting_wm_check, XCB_ATOM_WINDOW, 32, 1, &wm_check_win);

	xcb_atom_t utf8 = intern(conn, "UTF8_STRING");
	const char *name = "tinawm";
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, wm_check_win,
		atom_net_wm_name, utf8, 8, strlen(name), name);
}

void ewmh_update_client_list(xcb_connection_t *conn, xcb_window_t root)
{
	/* Count all clients across all monitors */
	int total = 0;
	for (Monitor *m = monitor_get_list(); m; m = m->next)
		for (Client *c = m->clients; c; c = c->next)
			total++;

	if (total == 0) {
		xcb_delete_property(conn, root, atom_net_client_list);
		return;
	}

	xcb_window_t *wins = malloc(sizeof(xcb_window_t) * total);
	if (!wins) return;

	int i = 0;
	for (Monitor *m = monitor_get_list(); m; m = m->next)
		for (Client *c = m->clients; c; c = c->next)
			wins[i++] = c->window;

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
		atom_net_client_list, XCB_ATOM_WINDOW, 32, total, wins);
	free(wins);
}

void ewmh_update_active_window(xcb_connection_t *conn, xcb_window_t root,
                                xcb_window_t win)
{
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
		atom_net_active_window, XCB_ATOM_WINDOW, 32, 1, &win);
}

void ewmh_update_desktops(xcb_connection_t *conn, xcb_window_t root,
                           int count, int current)
{
	uint32_t c = count;
	uint32_t cur = current;
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
		atom_net_number_of_desktops, XCB_ATOM_CARDINAL, 32, 1, &c);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
		atom_net_current_desktop, XCB_ATOM_CARDINAL, 32, 1, &cur);
}

xcb_atom_t ewmh_atom_net_wm_state(void) { return atom_net_wm_state; }
xcb_atom_t ewmh_atom_net_wm_state_fullscreen(void) { return atom_net_wm_state_fs; }
xcb_atom_t ewmh_atom_net_wm_window_type(void) { return atom_net_wm_window_type; }
xcb_atom_t ewmh_atom_net_wm_window_type_dialog(void) { return atom_net_wm_window_type_dialog; }
