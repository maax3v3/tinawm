#ifndef EWMH_H
#define EWMH_H

#include <xcb/xcb.h>

void ewmh_init(xcb_connection_t *conn, xcb_window_t root);
void ewmh_update_client_list(xcb_connection_t *conn, xcb_window_t root);
void ewmh_update_active_window(xcb_connection_t *conn, xcb_window_t root,
                                xcb_window_t win);
void ewmh_update_desktops(xcb_connection_t *conn, xcb_window_t root,
                           int count, int current);

/* Atoms accessible externally */
xcb_atom_t ewmh_atom_net_wm_state(void);
xcb_atom_t ewmh_atom_net_wm_state_fullscreen(void);
xcb_atom_t ewmh_atom_net_wm_window_type(void);
xcb_atom_t ewmh_atom_net_wm_window_type_dialog(void);

#endif /* EWMH_H */
