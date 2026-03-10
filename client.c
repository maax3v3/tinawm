#include "client.h"

#include <stdlib.h>

Client *client_create(xcb_window_t win)
{
	Client *c = calloc(1, sizeof(Client));
	if (!c)
		return NULL;
	c->window = win;
	return c;
}

void client_list_add(Client **list, Client *c)
{
	c->next = *list;
	*list = c;
}

void client_list_remove(Client **list, xcb_window_t win)
{
	Client **pp = list;
	while (*pp) {
		if ((*pp)->window == win) {
			Client *tmp = *pp;
			*pp = tmp->next;
			free(tmp);
			return;
		}
		pp = &(*pp)->next;
	}
}

int client_list_count(Client *list)
{
	int n = 0;
	for (Client *c = list; c; c = c->next)
		n++;
	return n;
}
