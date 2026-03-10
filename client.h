#ifndef CLIENT_H
#define CLIENT_H

#include <xcb/xcb.h>
#include <stdbool.h>

#define CLIENT_TITLE_MAX 256

typedef struct Client {
	xcb_window_t window;
	xcb_window_t frame;
	xcb_gcontext_t gc;
	int16_t x, y;
	uint16_t width, height;
	bool mapped;
	char title[CLIENT_TITLE_MAX];
	struct Client *next;
} Client;

/* Allocate a new client (not added to any list) */
Client *client_create(xcb_window_t win);

/* List operations on a given list head */
void client_list_add(Client **list, Client *c);
void client_list_remove(Client **list, xcb_window_t win);
int client_list_count(Client *list);

/* Global search across all monitors (defined in tinawm.c) */
Client *client_find(xcb_window_t win);
Client *client_find_by_frame(xcb_window_t frame);

#endif /* CLIENT_H */
