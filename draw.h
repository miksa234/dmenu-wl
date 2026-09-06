/* See LICENSE file for copyright and license details. */
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <cairo/cairo.h>
#include <stdbool.h>
#include <stddef.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#define MAX_MONITOR_NAME_LEN 255

#define FG(dc, col) ((col)[(dc)->invert ? ColBG : ColFG])
#define BG(dc, col) ((col)[(dc)->invert ? ColFG : ColBG])

enum { ColBG, ColFG, ColBorder, ColLast };

struct dmenu_panel;

enum dmenu_position {
    DMENU_POSITION_CENTER,
    DMENU_POSITION_TOP,
    DMENU_POSITION_BOTTOM
};

struct draw_buffer {
    struct dmenu_panel *panel;
    cairo_t *cairo;
    cairo_surface_t *cairo_surface;
    struct wl_buffer *buffer;
    void *data;
    size_t size;
    int32_t width;
    int32_t height;
    bool busy;
};

struct monitor_info {
    uint32_t registry_name;
    char name[MAX_MONITOR_NAME_LEN];
    struct wl_output *output;
    struct monitor_info *next;
};

struct display_info {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_seat *seat;
    uint32_t seat_registry_name;
    struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
    struct wp_viewporter *viewporter;
    struct monitor_info *monitors;
};

struct keyboard_info {
    struct wl_keyboard *kbd;
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    bool control;
    bool shift;
};

struct surface {
    struct wl_surface *surface;
    struct wl_shm *shm;
    struct draw_buffer buffers[2];
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wp_fractional_scale_v1 *fractional_scale;
    struct wp_viewport *viewport;
};

struct dmenu_panel {
    struct keyboard_info keyboard;
    /* struct monitor_info monitor; */
    int selected_monitor;
    char *selected_monitor_name;

    struct monitor_info *monitor;
    struct display_info display_info;

    struct surface surface;

    int32_t width;
    int32_t height;
    enum dmenu_position position;
    bool configured;
    bool closed;
    bool redraw_pending;
    uint32_t preferred_scale;

    int repeat_timer;
    int repeat_delay;
    int64_t repeat_period_ns;
    xkb_keysym_t repeat_sym;
    uint32_t repeat_key;

    bool running;
};

void dmenu_init_panel (struct dmenu_panel *, int32_t, int32_t,
                       enum dmenu_position);
void dmenu_draw (struct dmenu_panel *);
void dmenu_show (struct dmenu_panel *);
void dmenu_close (struct dmenu_panel *);

void draw_menu (cairo_t *, int32_t, int32_t, double);
void keypress (struct dmenu_panel *, enum wl_keyboard_key_state, xkb_keysym_t,
               bool, bool);
void pango_printf (cairo_t *cairo, const char *font, double scale,
                   const char *text);
void get_text_size (cairo_t *cairo, const char *font, int *width, int *height,
                    double scale, const char *text);
void eprintf (const char *fmt, ...);
void weprintf (const char *fmt, ...);
extern const char *progname;
