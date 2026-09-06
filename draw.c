/* See LICENSE file for copyright and license details. */
#include "draw.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <locale.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

static bool dmenu_create_buffer (struct dmenu_panel *panel,
                                 struct draw_buffer *buffer);
static void dmenu_destroy_buffer (struct draw_buffer *buffer);

static int32_t
scaled_size (int32_t logical, uint32_t scale)
{
    return ((int64_t)logical * scale + 119) / 120;
}

int
create_shm_file (off_t size)
{
    int fd = memfd_create ("dmenu-wl-buffer", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        return fd;
    }

    if (ftruncate (fd, size) < 0) {
        close (fd);
        return -1;
    }

    return fd;
}

PangoLayout *
get_pango_layout (cairo_t *cairo, const char *font, const char *text,
                  double scale)
{
    PangoLayout *layout = pango_cairo_create_layout (cairo);
    PangoAttrList *attrs = pango_attr_list_new ();
    pango_layout_set_text (layout, text, -1);
    pango_attr_list_insert (attrs, pango_attr_scale_new (scale));
    PangoFontDescription *desc = pango_font_description_from_string (font);
    pango_layout_set_font_description (layout, desc);
    pango_layout_set_single_paragraph_mode (layout, 1);
    pango_layout_set_attributes (layout, attrs);
    pango_attr_list_unref (attrs);
    pango_font_description_free (desc);
    return layout;
}

void
get_text_size (cairo_t *cairo, const char *font, int *width, int *height,
               double scale, const char *text)
{
    PangoLayout *layout = get_pango_layout (cairo, font, text, scale);
    pango_cairo_update_layout (cairo, layout);
    pango_layout_get_pixel_size (layout, width, height);
    g_object_unref (layout);
}

void
pango_printf (cairo_t *cairo, const char *font, double scale, const char *text)
{
    PangoLayout *layout = get_pango_layout (cairo, font, text, scale);
    cairo_font_options_t *fo = cairo_font_options_create ();
    cairo_get_font_options (cairo, fo);
    pango_cairo_context_set_font_options (pango_layout_get_context (layout),
                                          fo);
    cairo_font_options_destroy (fo);
    pango_cairo_update_layout (cairo, layout);
    pango_cairo_show_layout (cairo, layout);
    g_object_unref (layout);
}

void
dmenu_draw (struct dmenu_panel *panel)
{
    struct draw_buffer *buffer = NULL;
    for (size_t i = 0; i < 2; i++)
        if (!panel->surface.buffers[i].busy) {
            buffer = &panel->surface.buffers[i];
            break;
        }
    if (!buffer) {
        panel->redraw_pending = true;
        return;
    }
    int32_t pixel_width = scaled_size (panel->width, panel->preferred_scale);
    int32_t pixel_height = scaled_size (panel->height, panel->preferred_scale);
    if (!buffer->buffer || buffer->width != pixel_width
        || buffer->height != pixel_height) {
        dmenu_destroy_buffer (buffer);
        if (!dmenu_create_buffer (panel, buffer))
            eprintf ("cannot create drawing buffer\n");
    }
    panel->redraw_pending = false;

    cairo_t *cairo = buffer->cairo;
    cairo_set_operator (cairo, CAIRO_OPERATOR_CLEAR);
    cairo_paint (cairo);
    cairo_set_operator (cairo, CAIRO_OPERATOR_SOURCE);
    int32_t width = pixel_width;
    int32_t height = pixel_height;
    double scale = panel->preferred_scale / 120.0;

    draw_menu (cairo, width, height, scale);
    wl_surface_attach (panel->surface.surface, buffer->buffer, 0, 0);
    buffer->busy = true;
    wl_surface_damage_buffer (panel->surface.surface, 0, 0, width, height);
    wp_viewport_set_destination (panel->surface.viewport, panel->width,
                                 panel->height);
    wl_surface_commit (panel->surface.surface);
}

void
eprintf (const char *fmt, ...)
{
    va_list ap;

    fprintf (stderr, "%s: ", progname);
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
    exit (EXIT_FAILURE);
}

static void
layer_surface_configure (void *data, struct zwlr_layer_surface_v1 *surface,
                         uint32_t serial, uint32_t width, uint32_t height)
{
    struct dmenu_panel *panel = data;
    zwlr_layer_surface_v1_ack_configure (surface, serial);
    if (!panel->configured) {
        if (width)
            panel->width = width;
        if (height)
            panel->height = height;
    } else if ((width && width != (uint32_t)panel->width)
               || (height && height != (uint32_t)panel->height)) {
        if (width)
            panel->width = width;
        if (height)
            panel->height = height;
        panel->redraw_pending = true;
    }
    panel->configured = true;
    if (panel->surface.buffers[0].buffer)
        dmenu_draw (panel);
}

static void
layer_surface_closed (void *_data, struct zwlr_layer_surface_v1 *surface)
{
    (void)surface;
    struct dmenu_panel *panel = _data;
    panel->closed = true;
    panel->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void
output_geometry (void *data, struct wl_output *wl_output, int32_t x, int32_t y,
                 int32_t width_mm, int32_t height_mm, int32_t subpixel,
                 const char *make, const char *model, int32_t transform)
{
    (void)wl_output;
    (void)x;
    (void)y;
    (void)width_mm;
    (void)height_mm;
    (void)make;
    (void)model;
    (void)transform;
    (void)data;
    (void)subpixel;
}

static void
output_mode (void *data, struct wl_output *wl_output, uint32_t flags,
             int32_t width, int32_t height, int32_t refresh)
{
    (void)wl_output;
    (void)refresh;
    (void)data;
    (void)flags;
    (void)width;
    (void)height;
}

static void
output_done (void *data, struct wl_output *wl_output)
{
    (void)data;
    (void)wl_output;
}

static void
output_scale (void *data, struct wl_output *wl_output, int32_t factor)
{
    (void)wl_output;
    (void)data;
    (void)factor;
}

static void
output_name (void *data, struct wl_output *wl_output, const char *name)
{
    (void)wl_output;
    struct monitor_info *monitor = data;
    snprintf (monitor->name, sizeof monitor->name, "%s", name);
}

static void
output_description (void *data, struct wl_output *wl_output,
                    const char *description)
{
    (void)data;
    (void)wl_output;
    (void)description;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

static void
fractional_scale_preferred (void *data,
                            struct wp_fractional_scale_v1 *fractional_scale,
                            uint32_t scale)
{
    (void)fractional_scale;
    struct dmenu_panel *panel = data;
    if (panel->preferred_scale == scale)
        return;
    panel->preferred_scale = scale;
    panel->redraw_pending = true;
    if (panel->configured && panel->surface.buffers[0].buffer)
        dmenu_draw (panel);
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener
    = {
          .preferred_scale = fractional_scale_preferred,
      };

static void
keyboard_keymap (void *data, struct wl_keyboard *wl_keyboard, uint32_t format,
                 int32_t fd, uint32_t size)
{
    (void)wl_keyboard;
    struct dmenu_panel *panel = data;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close (fd);
        eprintf ("unsupported keyboard keymap format\n");
    }
    struct xkb_context *context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
    if (!context) {
        close (fd);
        eprintf ("cannot create XKB context\n");
    }
    char *map_shm = mmap (NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_shm == MAP_FAILED) {
        close (fd);
        xkb_context_unref (context);
        eprintf ("cannot map keyboard keymap\n");
    }
    struct xkb_keymap *keymap = xkb_keymap_new_from_string (
        context, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap (map_shm, size);
    close (fd);
    struct xkb_state *state = keymap ? xkb_state_new (keymap) : NULL;
    if (!keymap || !state) {
        xkb_state_unref (state);
        xkb_keymap_unref (keymap);
        xkb_context_unref (context);
        eprintf ("cannot compile keyboard keymap\n");
    }
    xkb_state_unref (panel->keyboard.xkb_state);
    xkb_keymap_unref (panel->keyboard.xkb_keymap);
    xkb_context_unref (panel->keyboard.xkb_context);
    panel->keyboard.xkb_context = context;
    panel->keyboard.xkb_keymap = keymap;
    panel->keyboard.xkb_state = state;
}

static void
cancel_key_repeat (struct dmenu_panel *panel)
{
    struct itimerspec spec = { 0 };
    timerfd_settime (panel->repeat_timer, 0, &spec, NULL);
}

static void
keyboard_enter (void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                struct wl_surface *surface, struct wl_array *keys)
{
    (void)data;
    (void)wl_keyboard;
    (void)serial;
    (void)surface;
    (void)keys;
}

static void
keyboard_leave (void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                struct wl_surface *surface)
{
    (void)wl_keyboard;
    (void)serial;
    (void)surface;
    struct dmenu_panel *panel = data;
    cancel_key_repeat (panel);
}

static void
keyboard_repeat (struct dmenu_panel *panel)
{
    keypress (panel, WL_KEYBOARD_KEY_STATE_PRESSED, panel->repeat_sym,
              panel->keyboard.control, panel->keyboard.shift);

    struct itimerspec spec = { 0 };
    spec.it_value.tv_sec = panel->repeat_period_ns / 1000000000;
    spec.it_value.tv_nsec = panel->repeat_period_ns % 1000000000;
    timerfd_settime (panel->repeat_timer, 0, &spec, NULL);
}

static void
keyboard_key (void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
              uint32_t time, uint32_t key, uint32_t _key_state)
{
    (void)wl_keyboard;
    (void)serial;
    (void)time;
    struct dmenu_panel *panel = data;
    if (!panel->keyboard.xkb_state || !panel->keyboard.xkb_keymap)
        return;

    enum wl_keyboard_key_state key_state = _key_state;
    xkb_keysym_t sym
        = xkb_state_key_get_one_sym (panel->keyboard.xkb_state, key + 8);
    keypress (panel, key_state, sym, panel->keyboard.control,
              panel->keyboard.shift);

    if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED
        && panel->repeat_period_ns > 0
        && xkb_keymap_key_repeats (panel->keyboard.xkb_keymap, key + 8)) {
        panel->repeat_sym = sym;
        panel->repeat_key = key;

        struct itimerspec spec = { 0 };
        spec.it_value.tv_sec = panel->repeat_delay / 1000;
        spec.it_value.tv_nsec = (panel->repeat_delay % 1000) * 1000000l;
        timerfd_settime (panel->repeat_timer, 0, &spec, NULL);
    } else if (key_state == WL_KEYBOARD_KEY_STATE_RELEASED
               && key == panel->repeat_key) {
        cancel_key_repeat (panel);
    }
}

static void
keyboard_repeat_info (void *data, struct wl_keyboard *wl_keyboard, int32_t rate,
                      int32_t delay)
{
    (void)wl_keyboard;
    struct dmenu_panel *panel = data;
    panel->repeat_delay = delay;
    if (rate > 0) {
        panel->repeat_period_ns = 1000000000LL / rate;
    } else {
        panel->repeat_period_ns = 0;
    }
}

static void
keyboard_modifiers (void *data, struct wl_keyboard *keyboard, uint32_t serial,
                    uint32_t mods_depressed, uint32_t mods_latched,
                    uint32_t mods_locked, uint32_t group)
{
    (void)keyboard;
    (void)serial;
    struct dmenu_panel *panel = data;
    xkb_state_update_mask (panel->keyboard.xkb_state, mods_depressed,
                           mods_latched, mods_locked, 0, 0, group);
    panel->keyboard.control = xkb_state_mod_name_is_active (
        panel->keyboard.xkb_state, XKB_MOD_NAME_CTRL,
        XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
    panel->keyboard.shift = xkb_state_mod_name_is_active (
        panel->keyboard.xkb_state, XKB_MOD_NAME_SHIFT,
        XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
}
static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void
seat_handle_capabilities (void *data, struct wl_seat *wl_seat,
                          enum wl_seat_capability caps)
{
    struct dmenu_panel *panel = data;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !panel->keyboard.kbd) {
        panel->keyboard.kbd = wl_seat_get_keyboard (wl_seat);
        wl_keyboard_add_listener (panel->keyboard.kbd, &keyboard_listener,
                                  panel);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && panel->keyboard.kbd) {
        cancel_key_repeat (panel);
        wl_keyboard_release (panel->keyboard.kbd);
        panel->keyboard.kbd = NULL;
    }
}
static void
seat_handle_name (void *data, struct wl_seat *wl_seat, const char *name)
{
    (void)data;
    (void)wl_seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

static void
handle_global (void *data, struct wl_registry *registry, uint32_t name,
               const char *interface, uint32_t version)
{
    struct dmenu_panel *panel = data;

    if (strcmp (interface, wl_compositor_interface.name) == 0) {
        if (version < 4)
            eprintf ("wl_compositor version 4 is required\n");
        panel->display_info.compositor
            = wl_registry_bind (registry, name, &wl_compositor_interface, 4);
    } else if (strcmp (interface, wl_seat_interface.name) == 0) {
        if (panel->display_info.seat)
            return;
        if (version < 5)
            eprintf ("wl_seat version 5 is required\n");
        panel->display_info.seat
            = wl_registry_bind (registry, name, &wl_seat_interface, 5);
        panel->display_info.seat_registry_name = name;
        wl_seat_add_listener (panel->display_info.seat, &seat_listener, panel);
    } else if (strcmp (interface, wl_shm_interface.name) == 0) {
        panel->surface.shm
            = wl_registry_bind (registry, name, &wl_shm_interface, 1);
    } else if (strcmp (interface, wl_output_interface.name) == 0) {
        if (version < 4)
            eprintf ("wl_output version 4 is required\n");
        struct monitor_info *monitor = calloc (1, sizeof *monitor);
        if (!monitor)
            eprintf ("cannot allocate output state\n");
        monitor->registry_name = name;
        monitor->output
            = wl_registry_bind (registry, name, &wl_output_interface, 4);
        struct monitor_info **link = &panel->display_info.monitors;
        while (*link)
            link = &(*link)->next;
        *link = monitor;
        wl_output_add_listener (monitor->output, &output_listener, monitor);
    } else if (strcmp (interface, zwlr_layer_shell_v1_interface.name) == 0) {
        if (version < 4)
            eprintf ("wlr-layer-shell version 4 is required\n");
        panel->surface.layer_shell = wl_registry_bind (
            registry, name, &zwlr_layer_shell_v1_interface, 4);
    } else if (strcmp (interface, wp_fractional_scale_manager_v1_interface.name)
               == 0) {
        panel->display_info.fractional_scale_manager = wl_registry_bind (
            registry, name, &wp_fractional_scale_manager_v1_interface, 1);
    } else if (strcmp (interface, wp_viewporter_interface.name) == 0) {
        panel->display_info.viewporter
            = wl_registry_bind (registry, name, &wp_viewporter_interface, 1);
    }
}

static void
handle_global_remove (void *data, struct wl_registry *registry, uint32_t name)
{
    (void)registry;
    struct dmenu_panel *panel = data;
    if (name == panel->display_info.seat_registry_name) {
        cancel_key_repeat (panel);
        if (panel->keyboard.kbd)
            wl_keyboard_release (panel->keyboard.kbd);
        panel->keyboard.kbd = NULL;
        wl_seat_release (panel->display_info.seat);
        panel->display_info.seat = NULL;
        panel->running = false;
        return;
    }
    struct monitor_info **link = &panel->display_info.monitors;
    while (*link) {
        struct monitor_info *monitor = *link;
        if (monitor->registry_name != name) {
            link = &monitor->next;
            continue;
        }
        if (panel->monitor == monitor) {
            panel->monitor = NULL;
            panel->redraw_pending = false;
            panel->running = false;
        }
        *link = monitor->next;
        wl_output_release (monitor->output);
        free (monitor);
        return;
    }
}

static void
buffer_release (void *data, struct wl_buffer *wl_buffer)
{
    (void)wl_buffer;
    struct draw_buffer *buffer = data;
    buffer->busy = false;
    if (buffer->panel->redraw_pending)
        dmenu_draw (buffer->panel);
}

static const struct wl_buffer_listener buffer_listener
    = { .release = buffer_release };

static void
dmenu_destroy_buffer (struct draw_buffer *buffer)
{
    if (buffer->cairo)
        cairo_destroy (buffer->cairo);
    if (buffer->cairo_surface)
        cairo_surface_destroy (buffer->cairo_surface);
    if (buffer->buffer)
        wl_buffer_destroy (buffer->buffer);
    if (buffer->data)
        munmap (buffer->data, buffer->size);
    memset (buffer, 0, sizeof *buffer);
}

static bool
dmenu_create_buffer (struct dmenu_panel *panel, struct draw_buffer *draw_buffer)
{
    int64_t scaled_width = scaled_size (panel->width, panel->preferred_scale);
    int64_t scaled_height = scaled_size (panel->height, panel->preferred_scale);
    if (scaled_width <= 0 || scaled_height <= 0 || scaled_width > INT32_MAX / 4
        || scaled_height > INT32_MAX)
        return false;
    int32_t width = scaled_width;
    int32_t height = scaled_height;

    int stride = width * 4;
    int64_t allocation_size = (int64_t)stride * height;
    if (allocation_size > INT32_MAX)
        return false;
    int size = allocation_size;

    int fd = create_shm_file (size);
    if (fd < 0) {
        fprintf (stderr, "creating a buffer file for %d B failed: %s\n", size,
                 strerror (errno));
        return false;
    }

    draw_buffer->data
        = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (draw_buffer->data == MAP_FAILED) {
        draw_buffer->data = NULL;
        fprintf (stderr, "mmap failed: %s\n", strerror (errno));
        close (fd);
        return false;
    }
    draw_buffer->size = size;

    struct wl_shm_pool *pool
        = wl_shm_create_pool (panel->surface.shm, fd, size);
    draw_buffer->buffer = wl_shm_pool_create_buffer (
        pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy (pool);
    close (fd);

    draw_buffer->panel = panel;
    draw_buffer->width = width;
    draw_buffer->height = height;
    wl_buffer_add_listener (draw_buffer->buffer, &buffer_listener, draw_buffer);

    cairo_surface_t *s = cairo_image_surface_create_for_data (
        draw_buffer->data, CAIRO_FORMAT_ARGB32, width, height, width * 4);
    draw_buffer->cairo = cairo_create (s);
    draw_buffer->cairo_surface = s;
    if (cairo_surface_status (s) != CAIRO_STATUS_SUCCESS
        || cairo_status (draw_buffer->cairo) != CAIRO_STATUS_SUCCESS) {
        dmenu_destroy_buffer (draw_buffer);
        return false;
    }
    cairo_set_antialias (draw_buffer->cairo, CAIRO_ANTIALIAS_BEST);
    cairo_font_options_t *fo = cairo_font_options_create ();
    cairo_font_options_set_hint_style (fo, CAIRO_HINT_STYLE_FULL);
    cairo_font_options_set_antialias (fo, CAIRO_ANTIALIAS_GRAY);
    cairo_set_font_options (draw_buffer->cairo, fo);
    cairo_font_options_destroy (fo);
    return true;
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

void
dmenu_init_panel (struct dmenu_panel *panel, int32_t width, int32_t height,
                  enum dmenu_position position)
{
    if (!setlocale (LC_CTYPE, ""))
        weprintf ("no locale support\n");

    if (!(panel->display_info.display = wl_display_connect (NULL)))
        eprintf ("cannot open display\n");

    if ((panel->repeat_timer = timerfd_create (CLOCK_MONOTONIC, 0)) < 0)
        eprintf ("cannot create timer fd\n");

    panel->width = width;
    panel->height = height;
    panel->position = position;
    panel->configured = false;
    panel->closed = false;
    panel->redraw_pending = false;
    panel->preferred_scale = 120;
    panel->repeat_period_ns = 0;
    panel->keyboard.control = false;

    panel->display_info.registry
        = wl_display_get_registry (panel->display_info.display);
    wl_registry_add_listener (panel->display_info.registry, &registry_listener,
                              panel);

    if (wl_display_roundtrip (panel->display_info.display) < 0)
        eprintf ("cannot enumerate Wayland globals\n");
    if (wl_display_roundtrip (panel->display_info.display) < 0)
        eprintf ("cannot read Wayland output metadata\n");
    if (!panel->display_info.compositor || !panel->surface.shm
        || !panel->surface.layer_shell
        || !panel->display_info.fractional_scale_manager
        || !panel->display_info.viewporter)
        eprintf ("compositor lacks a required modern Wayland protocol\n");
    if (!panel->display_info.seat)
        eprintf ("compositor did not advertise a keyboard seat\n");

    panel->monitor = NULL;
    int index = 0;
    for (struct monitor_info *monitor = panel->display_info.monitors; monitor;
         monitor = monitor->next, index++) {
        if ((!panel->selected_monitor_name && index == panel->selected_monitor)
            || (panel->selected_monitor_name
                && strcmp (panel->selected_monitor_name, monitor->name) == 0)) {
            panel->monitor = monitor;
            break;
        }
    }
    if (!panel->monitor) {
        if (!panel->selected_monitor_name)
            eprintf ("No monitor with index %i available.\n",
                     panel->selected_monitor);
        else
            eprintf ("No monitor with name %s available.\n",
                     panel->selected_monitor_name);
    }
    panel->surface.surface
        = wl_compositor_create_surface (panel->display_info.compositor);
    panel->surface.viewport = wp_viewporter_get_viewport (
        panel->display_info.viewporter, panel->surface.surface);
    panel->surface.fractional_scale
        = wp_fractional_scale_manager_v1_get_fractional_scale (
            panel->display_info.fractional_scale_manager,
            panel->surface.surface);
    wp_fractional_scale_v1_add_listener (panel->surface.fractional_scale,
                                         &fractional_scale_listener, panel);
    panel->surface.layer_surface = zwlr_layer_shell_v1_get_layer_surface (
        panel->surface.layer_shell, panel->surface.surface,
        panel->monitor->output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "dmenu-wl");

    bool bar = position != DMENU_POSITION_CENTER;
    zwlr_layer_surface_v1_set_size (panel->surface.layer_surface,
                                    bar ? 0 : panel->width, panel->height);
    if (bar)
        zwlr_layer_surface_v1_set_anchor (
            panel->surface.layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
                | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
                | (position == DMENU_POSITION_BOTTOM
                       ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
                       : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP));

    zwlr_layer_surface_v1_add_listener (panel->surface.layer_surface,
                                        &layer_surface_listener, panel);
    zwlr_layer_surface_v1_set_keyboard_interactivity (
        panel->surface.layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);

    wl_surface_commit (panel->surface.surface);
    while (!panel->configured && !panel->closed
           && wl_display_dispatch (panel->display_info.display) >= 0)
        ;
    if (panel->closed)
        eprintf ("layer surface was closed by the compositor\n");
    if (!panel->configured)
        eprintf ("layer surface was not configured\n");

    if (!dmenu_create_buffer (panel, &panel->surface.buffers[0])
        || !dmenu_create_buffer (panel, &panel->surface.buffers[1]))
        eprintf ("cannot create drawing buffer\n");
}

void
dmenu_show (struct dmenu_panel *dmenu)
{
    dmenu_draw (dmenu);

    struct pollfd fds[] = {
        { .fd = wl_display_get_fd (dmenu->display_info.display),
          .events = POLLIN },
        { .fd = dmenu->repeat_timer, .events = POLLIN },
    };
    const int nfds = sizeof (fds) / sizeof (*fds);

    dmenu->running = true;
    while (dmenu->running) {
        while (wl_display_prepare_read (dmenu->display_info.display) != 0) {
            if (wl_display_dispatch_pending (dmenu->display_info.display) < 0) {
                dmenu->running = false;
                break;
            }
        }
        if (!dmenu->running)
            break;

        fds[0].events = POLLIN;
        if (wl_display_flush (dmenu->display_info.display) < 0) {
            if (errno == EAGAIN)
                fds[0].events |= POLLOUT;
            else {
                wl_display_cancel_read (dmenu->display_info.display);
                break;
            }
        }

        if (poll (fds, nfds, -1) < 0) {
            wl_display_cancel_read (dmenu->display_info.display);
            if (errno == EINTR)
                continue;
            break;
        }

        if (fds[0].revents & POLLIN) {
            if (wl_display_read_events (dmenu->display_info.display) < 0)
                dmenu->running = false;
        } else {
            wl_display_cancel_read (dmenu->display_info.display);
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
            dmenu->running = false;
        if (dmenu->running
            && wl_display_dispatch_pending (dmenu->display_info.display) < 0)
            dmenu->running = false;

        if (fds[1].revents & POLLIN) {
            uint64_t expirations;
            ssize_t bytes
                = read (dmenu->repeat_timer, &expirations, sizeof expirations);
            if (bytes != sizeof expirations)
                break;
            keyboard_repeat (dmenu);
        }
    }

    cancel_key_repeat (dmenu);
    if (dmenu->keyboard.kbd)
        wl_keyboard_release (dmenu->keyboard.kbd);
    xkb_state_unref (dmenu->keyboard.xkb_state);
    xkb_keymap_unref (dmenu->keyboard.xkb_keymap);
    xkb_context_unref (dmenu->keyboard.xkb_context);
    for (size_t i = 0; i < 2; i++)
        dmenu_destroy_buffer (&dmenu->surface.buffers[i]);
    if (dmenu->surface.fractional_scale)
        wp_fractional_scale_v1_destroy (dmenu->surface.fractional_scale);
    if (dmenu->surface.viewport)
        wp_viewport_destroy (dmenu->surface.viewport);
    if (dmenu->surface.layer_surface)
        zwlr_layer_surface_v1_destroy (dmenu->surface.layer_surface);
    if (dmenu->surface.surface)
        wl_surface_destroy (dmenu->surface.surface);
    struct monitor_info *monitor = dmenu->display_info.monitors;
    while (monitor) {
        struct monitor_info *next = monitor->next;
        wl_output_release (monitor->output);
        free (monitor);
        monitor = next;
    }
    if (dmenu->display_info.seat)
        wl_seat_release (dmenu->display_info.seat);
    if (dmenu->display_info.fractional_scale_manager)
        wp_fractional_scale_manager_v1_destroy (
            dmenu->display_info.fractional_scale_manager);
    if (dmenu->display_info.viewporter)
        wp_viewporter_destroy (dmenu->display_info.viewporter);
    if (dmenu->surface.layer_shell)
        zwlr_layer_shell_v1_destroy (dmenu->surface.layer_shell);
    if (dmenu->surface.shm)
        wl_shm_release (dmenu->surface.shm);
    if (dmenu->display_info.compositor)
        wl_compositor_destroy (dmenu->display_info.compositor);
    if (dmenu->display_info.registry)
        wl_registry_destroy (dmenu->display_info.registry);
    close (dmenu->repeat_timer);
    wl_display_flush (dmenu->display_info.display);
    wl_display_disconnect (dmenu->display_info.display);
}
void
dmenu_close (struct dmenu_panel *dmenu)
{
    dmenu->running = false;
}

void
weprintf (const char *fmt, ...)
{
    va_list ap;

    fprintf (stderr, "%s: warning: ", progname);
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
}
