#ifndef KEYS_H
#define KEYS_H

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

void keys_init(xcb_connection_t *conn, xcb_window_t root);
void keys_handle(xcb_connection_t *conn, xcb_key_press_event_t *ev);
void keys_cleanup(void);

#endif /* KEYS_H */
