#include "keys.h"
#include "config.h"
#include "util.h"

#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>

static xcb_key_symbols_t *keysyms = NULL;
static xcb_connection_t *dpy = NULL;
static unsigned int numlock_mask = 0;

typedef struct {
	unsigned int mod;
	xcb_keysym_t keysym;
	void (*func)(void);
} Keybind;

/* Detect which modifier bit corresponds to Num_Lock */
static void detect_numlock(xcb_connection_t *conn)
{
	numlock_mask = 0;
	xcb_get_modifier_mapping_cookie_t mc = xcb_get_modifier_mapping(conn);
	xcb_get_modifier_mapping_reply_t *mr = xcb_get_modifier_mapping_reply(conn, mc, NULL);
	if (!mr)
		return;

	xcb_keycode_t *modmap = xcb_get_modifier_mapping_keycodes(mr);
	int width = mr->keycodes_per_modifier;

	xcb_keycode_t *nl_codes = xcb_key_symbols_get_keycode(keysyms, XK_Num_Lock);
	if (nl_codes) {
		for (int mod = 0; mod < 8; mod++) {
			for (int j = 0; j < width; j++) {
				xcb_keycode_t kc = modmap[mod * width + j];
				if (kc == XCB_NO_SYMBOL)
					continue;
				for (xcb_keycode_t *n = nl_codes; *n != XCB_NO_SYMBOL; n++) {
					if (*n == kc) {
						numlock_mask = (1 << mod);
						goto found;
					}
				}
			}
		}
	found:
		free(nl_codes);
	}
	free(mr);
}

#define CLEANMASK(mask) ((mask) & ~(numlock_mask | XCB_MOD_MASK_LOCK))

static void key_spawn_terminal(void)
{
	spawn(TERMINAL);
}

/* Defined in tinawm.c */
extern void wm_close_focused(void);
extern void wm_toggle_layout(void);
extern void wm_focus_dir(int dx, int dy);
extern void wm_focus_monitor(int num);
extern void wm_open_launcher(void);
extern void wm_quit(void);

static void key_close_window(void)  { wm_close_focused(); }
static void key_toggle_layout(void) { wm_toggle_layout(); }
static void key_focus_up(void)      { wm_focus_dir(0, -1); }
static void key_focus_down(void)    { wm_focus_dir(0, 1); }
static void key_focus_left(void)    { wm_focus_dir(-1, 0); }
static void key_focus_right(void)   { wm_focus_dir(1, 0); }
static void key_launcher(void)      { wm_open_launcher(); }
static void key_quit(void)          { wm_quit(); }
static void key_mon_1(void)         { wm_focus_monitor(0); }
static void key_mon_2(void)         { wm_focus_monitor(1); }
static void key_mon_3(void)         { wm_focus_monitor(2); }
static void key_mon_4(void)         { wm_focus_monitor(3); }
static void key_mon_5(void)         { wm_focus_monitor(4); }
static void key_mon_6(void)         { wm_focus_monitor(5); }
static void key_mon_7(void)         { wm_focus_monitor(6); }
static void key_mon_8(void)         { wm_focus_monitor(7); }
static void key_mon_9(void)         { wm_focus_monitor(8); }

static const Keybind keybinds[] = {
	{ MOD_KEY,                        XK_Return, key_spawn_terminal },
	{ MOD_KEY | XCB_MOD_MASK_SHIFT,   XK_q,      key_close_window },
	{ MOD_KEY | XCB_MOD_MASK_SHIFT,   XK_e,      key_quit },
	{ MOD_KEY,                        XK_d,      key_launcher },
	{ MOD_KEY,                        XK_space,  key_toggle_layout },
	{ MOD_KEY,                        XK_Up,     key_focus_up },
	{ MOD_KEY,                        XK_Down,   key_focus_down },
	{ MOD_KEY,                        XK_Left,   key_focus_left },
	{ MOD_KEY,                        XK_Right,  key_focus_right },
	{ MOD_KEY,                        XK_1,      key_mon_1 },
	{ MOD_KEY,                        XK_2,      key_mon_2 },
	{ MOD_KEY,                        XK_3,      key_mon_3 },
	{ MOD_KEY,                        XK_4,      key_mon_4 },
	{ MOD_KEY,                        XK_5,      key_mon_5 },
	{ MOD_KEY,                        XK_6,      key_mon_6 },
	{ MOD_KEY,                        XK_7,      key_mon_7 },
	{ MOD_KEY,                        XK_8,      key_mon_8 },
	{ MOD_KEY,                        XK_9,      key_mon_9 },
};

#define KEYBIND_COUNT (sizeof(keybinds) / sizeof(keybinds[0]))

void keys_init(xcb_connection_t *conn, xcb_window_t root)
{
	dpy = conn;
	keysyms = xcb_key_symbols_alloc(conn);
	if (!keysyms)
		die("failed to allocate key symbols");

	detect_numlock(conn);

	/* Grab each key with all combinations of NumLock and CapsLock */
	unsigned int lockmasks[] = { 0, XCB_MOD_MASK_LOCK, numlock_mask,
	                             numlock_mask | XCB_MOD_MASK_LOCK };

	for (size_t i = 0; i < KEYBIND_COUNT; i++) {
		xcb_keycode_t *keycode = xcb_key_symbols_get_keycode(keysyms,
			keybinds[i].keysym);
		if (!keycode)
			continue;
		for (xcb_keycode_t *kc = keycode; *kc != XCB_NO_SYMBOL; kc++) {
			for (size_t l = 0; l < 4; l++) {
				xcb_grab_key(conn, 1, root,
					keybinds[i].mod | lockmasks[l], *kc,
					XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
			}
		}
		free(keycode);
	}
	xcb_flush(conn);
}

void keys_handle(xcb_connection_t *conn __attribute__((unused)), xcb_key_press_event_t *ev)
{
	xcb_keysym_t sym = xcb_key_symbols_get_keysym(keysyms, ev->detail, 0);
	unsigned int state = CLEANMASK(ev->state);

	for (size_t i = 0; i < KEYBIND_COUNT; i++) {
		if (keybinds[i].keysym == sym &&
		    CLEANMASK(keybinds[i].mod) == state) {
			keybinds[i].func();
			return;
		}
	}
}

void keys_cleanup(void)
{
	if (keysyms) {
		xcb_key_symbols_free(keysyms);
		keysyms = NULL;
	}
}
