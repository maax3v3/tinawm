#ifndef CONFIG_H
#define CONFIG_H

/* Terminal command */
#define TERMINAL "xterm"

/* Modifier key (Super/Win) */
#define MOD_KEY XCB_MOD_MASK_4

/* Colors (ARGB format, 0x00RRGGBB) */
#define COLOR_BORDER_FOCUSED   0x4C7899
#define COLOR_BORDER_UNFOCUSED 0x333333
#define COLOR_TITLEBAR_BG      0x285577
#define COLOR_TITLEBAR_FG      0xFFFFFF
#define COLOR_BAR_BG           0x1D1F21
#define COLOR_BAR_FG           0xC5C8C6

/* Dimensions */
#define BORDER_WIDTH   1
#define TITLEBAR_HEIGHT 20
#define BAR_HEIGHT     20

/* Font */
#define FONT_NAME "fixed"

#endif /* CONFIG_H */
