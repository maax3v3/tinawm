#include "layout.h"
#include "monitor.h"
#include "client.h"
#include "config.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

/* Build a flat array of clients from a monitor's client list.
 * Caller must free the returned array. */
static Client **build_client_array(Monitor *m, int *out_n)
{
	int n = client_list_count(m->clients);
	if (n == 0) {
		*out_n = 0;
		return NULL;
	}
	Client **arr = malloc(sizeof(Client *) * n);
	if (!arr) {
		*out_n = 0;
		return NULL;
	}
	int i = 0;
	for (Client *c = m->clients; c; c = c->next)
		arr[i++] = c;
	*out_n = n;
	return arr;
}

/* Position a client's frame and resize the client window inside it. */
static void position_client(xcb_connection_t *conn, Client *c,
                             int fx, int fy, int cw, int ch)
{
	if (cw < 1) cw = 1;
	if (ch < 1) ch = 1;

	c->x = fx;
	c->y = fy;
	c->width = cw;
	c->height = ch;

	uint16_t frame_w = cw + 2 * BORDER_WIDTH;
	uint16_t frame_h = ch + TITLEBAR_HEIGHT + BORDER_WIDTH;

	if (c->frame) {
		uint32_t fvals[] = { fx, fy, frame_w, frame_h };
		xcb_configure_window(conn, c->frame,
			XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
			XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
			fvals);

		uint32_t cvals[] = { cw, ch };
		xcb_configure_window(conn, c->window,
			XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
			cvals);
	}
}

static void apply_grid(xcb_connection_t *conn, Client **arr, int n,
                        int sx, int sy, int sw, int sh)
{
	if (n == 0)
		return;

	int cols = (int)ceil(sqrt((double)n));
	int rows = (int)ceil((double)n / cols);

	int cell_w = sw / cols;
	int cell_h = sh / rows;

	for (int i = 0; i < n; i++) {
		int col = i % cols;
		int row = i / cols;

		int fx = sx + col * cell_w;
		int fy = sy + row * cell_h;
		int cw = cell_w - 2 * BORDER_WIDTH;
		int ch = cell_h - TITLEBAR_HEIGHT - BORDER_WIDTH;

		position_client(conn, arr[i], fx, fy, cw, ch);

		if (!arr[i]->mapped && arr[i]->frame) {
			xcb_map_window(conn, arr[i]->frame);
			xcb_map_window(conn, arr[i]->window);
			arr[i]->mapped = true;
		}
	}
}

static void apply_monocle(xcb_connection_t *conn, int focused_idx,
                           Client **arr, int n,
                           int sx, int sy, int sw, int sh)
{
	if (n == 0)
		return;

	int cw = sw - 2 * BORDER_WIDTH;
	int ch = sh - TITLEBAR_HEIGHT - BORDER_WIDTH;

	for (int i = 0; i < n; i++) {
		if (i == focused_idx) {
			position_client(conn, arr[i], sx, sy, cw, ch);

			if (!arr[i]->mapped && arr[i]->frame) {
				xcb_map_window(conn, arr[i]->frame);
				xcb_map_window(conn, arr[i]->window);
				arr[i]->mapped = true;
			}
		} else {
			if (arr[i]->mapped && arr[i]->frame) {
				xcb_unmap_window(conn, arr[i]->frame);
				arr[i]->mapped = false;
			}
		}
	}
}

void layout_apply(xcb_connection_t *conn, Monitor *m)
{
	int n;
	Client **arr = build_client_array(m, &n);
	if (!arr || n == 0) {
		free(arr);
		return;
	}

	if (m->focused_idx >= n)
		m->focused_idx = n - 1;

	int sx = m->x;
	int sy = m->y + BAR_HEIGHT;
	int sw = m->w;
	int sh = m->h - BAR_HEIGHT;

	switch (m->mode) {
	case LAYOUT_GRID:
		apply_grid(conn, arr, n, sx, sy, sw, sh);
		break;
	case LAYOUT_MONOCLE:
		apply_monocle(conn, m->focused_idx, arr, n, sx, sy, sw, sh);
		break;
	}

	xcb_flush(conn);
	free(arr);
}

void layout_focus_dir(xcb_connection_t *conn, Monitor *m, int dx, int dy)
{
	(void)conn;

	int n;
	Client **arr = build_client_array(m, &n);
	if (!arr || n == 0) {
		free(arr);
		return;
	}

	if (m->focused_idx >= n)
		m->focused_idx = n - 1;
	if (m->focused_idx < 0)
		m->focused_idx = 0;

	if (m->mode == LAYOUT_MONOCLE) {
		int dir = (dx > 0 || dy < 0) ? 1 : -1;
		m->focused_idx = (m->focused_idx + dir + n) % n;
	} else {
		int cols = (int)ceil(sqrt((double)n));
		int row = m->focused_idx / cols;
		int col = m->focused_idx % cols;

		int new_row = row + dy;
		int new_col = col + dx;

		if (new_row < 0 || new_col < 0)
			goto done;

		int rows = (int)ceil((double)n / cols);
		if (new_row >= rows || new_col >= cols)
			goto done;

		int new_idx = new_row * cols + new_col;
		if (new_idx >= n)
			goto done;

		m->focused_idx = new_idx;
	}

done:
	free(arr);
}
