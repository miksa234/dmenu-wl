/* See LICENSE file for copyright and license details. */
#include <locale.h>
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <pango/pangocairo.h>
#include <xkbcommon/xkbcommon.h>
#include "draw.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"


static const char overflow[] = "[buffer overflow]";
static const int max_chars = 16384;

struct monitor_info *monitors[16] = {0};
static int n_monitors = 0;

static bool dmenu_create_buffer(struct dmenu_panel *panel,
		struct draw_buffer *buffer);
static void dmenu_destroy_buffer(struct draw_buffer *buffer);

int32_t round_to_int(double val) {
	return (int32_t)(val + 0.5);
}

static void randname(char *buf) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	long r = ts.tv_nsec;
	for (int i = 0; i < 6; ++i) {
		buf[i] = 'A'+(r&15)+(r&16)*2;
		r >>= 5;
	}
}

static int anonymous_shm_open(void) {
	char name[] = "/dmenu-XXXXXX";
	int retries = 100;

	do {
		randname(name + strlen(name) - 6);

		--retries;
		// shm_open guarantees that O_CLOEXEC is set
		int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			shm_unlink(name);
			return fd;
		}
	} while (retries > 0 && errno == EEXIST);

	return -1;
}

int create_shm_file(off_t size) {
	int fd = anonymous_shm_open();
	if (fd < 0) {
		return fd;
	}

	if (ftruncate(fd, size) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

PangoLayout *get_pango_layout(cairo_t *cairo, const char *font,
		const char *text, double scale, bool markup) {
	PangoLayout *layout = pango_cairo_create_layout(cairo);
	PangoAttrList *attrs;
	if (markup) {
		char *buf;
		GError *error = NULL;
		if (pango_parse_markup(text, -1, 0, &attrs, &buf, NULL, &error)) {
			pango_layout_set_text(layout, buf, -1);
			free(buf);
		} else {
			/* wlr_log(WLR_ERROR, "pango_parse_markup '%s' -> error %s", text, */
			/* 		error->message); */
			g_error_free(error);
			markup = false; // fallback to plain text
		}
	}
	if (!markup) {
		attrs = pango_attr_list_new();
		pango_layout_set_text(layout, text, -1);
	}

	pango_attr_list_insert(attrs, pango_attr_scale_new(scale));
	PangoFontDescription *desc = pango_font_description_from_string(font);
	pango_layout_set_font_description(layout, desc);
	pango_layout_set_single_paragraph_mode(layout, 1);
	pango_layout_set_attributes(layout, attrs);
	pango_attr_list_unref(attrs);
	pango_font_description_free(desc);
	return layout;
}

void get_text_size(cairo_t *cairo, const char *font, int *width, int *height,
		int *baseline, double scale, bool markup, const char *fmt, ...) {
	char buf[max_chars];

	va_list args;
	va_start(args, fmt);
	if (vsnprintf(buf, sizeof(buf), fmt, args) >= max_chars) {
		strcpy(&buf[sizeof(buf) - sizeof(overflow)], overflow);
	}
	va_end(args);

	PangoLayout *layout = get_pango_layout(cairo, font, buf, scale, markup);
	pango_cairo_update_layout(cairo, layout);
	pango_layout_get_pixel_size(layout, width, height);
	if (baseline) {
		*baseline = pango_layout_get_baseline(layout) / PANGO_SCALE;
	}
	g_object_unref(layout);
}

void pango_printf(cairo_t *cairo, const char *font,
		double scale, bool markup, const char *fmt, ...) {
	char buf[max_chars];

	va_list args;
	va_start(args, fmt);
	if (vsnprintf(buf, sizeof(buf), fmt, args) >= max_chars) {
		strcpy(&buf[sizeof(buf) - sizeof(overflow)], overflow);
	}
	va_end(args);

	PangoLayout *layout = get_pango_layout(cairo, font, buf, scale, markup);
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_get_font_options(cairo, fo);
	pango_cairo_context_set_font_options(pango_layout_get_context(layout), fo);
	cairo_font_options_destroy(fo);
	pango_cairo_update_layout(cairo, layout);
	pango_cairo_show_layout(cairo, layout);
	g_object_unref(layout);
}


void dmenu_draw(struct dmenu_panel *panel) {
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
	int32_t pixel_width = panel->width * panel->monitor->scale;
	int32_t pixel_height = panel->height * panel->monitor->scale;
	if (!buffer->buffer || buffer->width != pixel_width ||
			buffer->height != pixel_height) {
		dmenu_destroy_buffer(buffer);
		if (!dmenu_create_buffer(panel, buffer))
			eprintf("cannot create drawing buffer\n");
	}
	panel->redraw_pending = false;

	cairo_t *cairo = buffer->cairo;
	cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	struct monitor_info *m = panel->monitor;
	int32_t width = panel->width * m->scale;
	int32_t height = panel->height * m->scale;

	if (panel->draw) {
		panel->draw(cairo, width, height, m->scale);
	}
	wl_surface_attach(panel->surface.surface, buffer->buffer, 0, 0);
	buffer->busy = true;
	wl_surface_damage(panel->surface.surface, 0, 0, panel->width, panel->height);
	wl_surface_commit(panel->surface.surface);

}

cairo_subpixel_order_t to_cairo_subpixel_order(enum wl_output_subpixel subpixel) {
	switch (subpixel) {
	case WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB:
		return CAIRO_SUBPIXEL_ORDER_RGB;
	case WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR:
		return CAIRO_SUBPIXEL_ORDER_BGR;
	case WL_OUTPUT_SUBPIXEL_VERTICAL_RGB:
		return CAIRO_SUBPIXEL_ORDER_VRGB;
	case WL_OUTPUT_SUBPIXEL_VERTICAL_BGR:
		return CAIRO_SUBPIXEL_ORDER_VBGR;
	default:
		return CAIRO_SUBPIXEL_ORDER_DEFAULT;
	}
	return CAIRO_SUBPIXEL_ORDER_DEFAULT;
}

void
eprintf(const char *fmt, ...) {
	va_list ap;

	fprintf(stderr, "%s: ", progname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static void layer_surface_configure(void *data,
		struct zwlr_layer_surface_v1 *surface,
		uint32_t serial, uint32_t width, uint32_t height) {
	struct dmenu_panel *panel = data;
	zwlr_layer_surface_v1_ack_configure(surface, serial);
	if (!panel->configured) {
		if (width)
			panel->width = width;
		if (height)
			panel->height = height;
	} else if ((width && width != (uint32_t)panel->width) ||
			(height && height != (uint32_t)panel->height)) {
		if (width)
			panel->width = width;
		if (height)
			panel->height = height;
		panel->redraw_pending = true;
	}
	panel->configured = true;
	if (panel->surface.buffers[0].buffer)
		dmenu_draw(panel);
}

static void layer_surface_closed(void *_data,
		struct zwlr_layer_surface_v1 *surface) {
	struct dmenu_panel *panel = _data;
	panel->closed = true;
	panel->running = false;
}

struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};


int32_t subpixel;
int32_t physical_height;


static void output_geometry(void *data, struct wl_output *wl_output, int32_t x,
		int32_t y, int32_t width_mm, int32_t height_mm, int32_t subpixel,
		const char *make, const char *model, int32_t transform) {
	struct monitor_info *monitor = data;
	monitor->subpixel = subpixel;
}

static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags,
		int32_t width, int32_t height, int32_t refresh) {
	struct monitor_info *monitor = data;
	if (flags & WL_OUTPUT_MODE_CURRENT) {
		monitor->physical_width = width;
		monitor->physical_height = height;
	}
}

static void output_done(void *data, struct wl_output *wl_output) {
}

static void output_scale(void *data, struct wl_output *wl_output,
		int32_t factor) {
	struct monitor_info *monitor = data;
	monitor->scale = factor;
}

struct wl_output_listener output_listener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
};
static void xdg_output_handle_logical_position(void *data,
		struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y) {
	// Who cares
}

static void xdg_output_handle_logical_size(void *data,
		struct zxdg_output_v1 *xdg_output, int32_t width, int32_t height) {
	struct monitor_info *monitor = data;

	monitor->logical_width = width;
	monitor->logical_height = height;
}

static void xdg_output_handle_done(void *data,
		struct zxdg_output_v1 *xdg_output) {
	// Who cares
}

static void xdg_output_handle_name(void *data,
		struct zxdg_output_v1 *xdg_output, const char *name) {
	struct monitor_info *monitor = data;
	strncpy(monitor->name, name, MAX_MONITOR_NAME_LEN - 1);
	monitor->name[MAX_MONITOR_NAME_LEN - 1] = '\0';
}

static void xdg_output_handle_description(void *data,
		struct zxdg_output_v1 *xdg_output, const char *description) {
}

struct zxdg_output_v1_listener xdg_output_listener = {
	.logical_position = xdg_output_handle_logical_position,
	.logical_size = xdg_output_handle_logical_size,
	.done = xdg_output_handle_done,
	.name = xdg_output_handle_name,
	.description = xdg_output_handle_description,
};

static void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t format, int32_t fd, uint32_t size) {

	struct dmenu_panel *panel = data;

	panel->keyboard.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		exit(1);
	}
	char *map_shm = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (map_shm == MAP_FAILED) {
		close(fd);
		exit(1);
	}
	panel->keyboard.xkb_keymap = xkb_keymap_new_from_string(
			panel->keyboard.xkb_context, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
	munmap(map_shm, size);
	close(fd);

	panel->keyboard.xkb_state = xkb_state_new(panel->keyboard.xkb_keymap);
}

static void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	// Who cares
}

static void keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface) {
	// Who cares
}

static void keyboard_repeat(struct dmenu_panel *panel) {
	if (panel->on_keyrepeat) {
		panel->on_keyrepeat(panel);
	}

	struct itimerspec spec = { 0 };
	spec.it_value.tv_sec = panel->repeat_period / 1000;
	spec.it_value.tv_nsec = (panel->repeat_period % 1000) * 1000000l;
	timerfd_settime(panel->repeat_timer, 0, &spec, NULL);
}

static void keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t _key_state) {
	struct dmenu_panel *panel = data;

	enum wl_keyboard_key_state key_state = _key_state;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(panel->keyboard.xkb_state, key + 8);
	if (panel->on_keyevent) {
		panel->on_keyevent(panel, key_state, sym, panel->keyboard.control,
						   panel->keyboard.shift);

		if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED && panel->repeat_period >= 0 &&
				xkb_keymap_key_repeats(panel->keyboard.xkb_keymap, key + 8)) {
			panel->repeat_key_state = key_state;
			panel->repeat_sym = sym;
			panel->repeat_key = key;

			struct itimerspec spec = { 0 };
			spec.it_value.tv_sec = panel->repeat_delay / 1000;
			spec.it_value.tv_nsec = (panel->repeat_delay % 1000) * 1000000l;
			timerfd_settime(panel->repeat_timer, 0, &spec, NULL);
		} else if (key_state == WL_KEYBOARD_KEY_STATE_RELEASED &&
				key == panel->repeat_key) {
			struct itimerspec spec = { 0 };
			timerfd_settime(panel->repeat_timer, 0, &spec, NULL);
		}
	}
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
		int32_t rate, int32_t delay) {
	struct dmenu_panel *panel = data;
	panel->repeat_delay = delay;
	if (rate > 0) {
		panel->repeat_period = 1000 / rate;
	} else {
		panel->repeat_period = -1;
	}
}

static void keyboard_modifiers (void *data, struct wl_keyboard *keyboard,
								uint32_t serial, uint32_t mods_depressed,
								uint32_t mods_latched, uint32_t mods_locked,
								uint32_t group) {
	struct dmenu_panel *panel = data;
	xkb_state_update_mask(panel->keyboard.xkb_state,
		mods_depressed, mods_latched, mods_locked, 0, 0, group);
	panel->keyboard.control = xkb_state_mod_name_is_active(panel->keyboard.xkb_state,
		XKB_MOD_NAME_CTRL,
		XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
	panel->keyboard.shift = xkb_state_mod_name_is_active(panel->keyboard.xkb_state,
		XKB_MOD_NAME_SHIFT,
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

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps) {
	struct dmenu_panel *panel = data;
	if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
		panel->keyboard.kbd = wl_seat_get_keyboard (panel->display_info.seat);
		wl_keyboard_add_listener (panel->keyboard.kbd, &keyboard_listener, panel);
	}
}
static void seat_handle_name(void *data, struct wl_seat *wl_seat,
		const char *name) {
	// Who cares
}

const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};

void set_monitor_xdg_output(struct dmenu_panel *panel, struct monitor_info *monitor){
	monitor->xdg_output =
		zxdg_output_manager_v1_get_xdg_output(panel->display_info.xdg_output_manager,
												monitor->output);
	zxdg_output_v1_add_listener(monitor->xdg_output, &xdg_output_listener,
								monitor);
}

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct dmenu_panel *panel = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		panel->display_info.compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		panel->display_info.seat = wl_registry_bind (registry, name, &wl_seat_interface, 4);
		wl_seat_add_listener (panel->display_info.seat, &seat_listener, panel);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		panel->surface.shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {

		if(n_monitors >= 16) return;

		monitors[n_monitors] = calloc(1, sizeof(struct monitor_info));
		monitors[n_monitors]->panel = panel;
		monitors[n_monitors]->scale = 1;
		monitors[n_monitors]->output = wl_registry_bind(registry, name, &wl_output_interface, 2);

		wl_output_add_listener(monitors[n_monitors]->output, &output_listener,
							   monitors[n_monitors]);

		if (panel->display_info.xdg_output_manager != NULL) {
			set_monitor_xdg_output(panel, monitors[n_monitors]);
		}
		n_monitors++;

	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		panel->surface.layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);

	} else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
		panel->display_info.xdg_output_manager = wl_registry_bind(registry, name,
			&zxdg_output_manager_v1_interface, 2);

		for(int m = 0; m < n_monitors; m++){
			set_monitor_xdg_output(panel, monitors[m]);
		}
	}

}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
	struct draw_buffer *buffer = data;
	buffer->busy = false;
	if (buffer->panel->redraw_pending)
		dmenu_draw(buffer->panel);
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_release
};

static void
dmenu_destroy_buffer(struct draw_buffer *buffer) {
	if (buffer->cairo)
		cairo_destroy(buffer->cairo);
	if (buffer->cairo_surface)
		cairo_surface_destroy(buffer->cairo_surface);
	if (buffer->buffer)
		wl_buffer_destroy(buffer->buffer);
	if (buffer->data)
		munmap(buffer->data, buffer->size);
	memset(buffer, 0, sizeof *buffer);
}

static bool dmenu_create_buffer(struct dmenu_panel *panel,
		struct draw_buffer *draw_buffer) {
	struct monitor_info *m = panel->monitor;
	int64_t scaled_width = (int64_t)panel->width * m->scale;
	int64_t scaled_height = (int64_t)panel->height * m->scale;
	if (scaled_width <= 0 || scaled_height <= 0 ||
			scaled_width > INT32_MAX / 4 || scaled_height > INT32_MAX)
		return false;
	int32_t width = scaled_width;
	int32_t height = scaled_height;

	int stride = width * 4;
	int64_t allocation_size = (int64_t)stride * height;
	if (allocation_size > INT32_MAX)
		return false;
	int size = allocation_size;

	int fd = create_shm_file(size);
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %s\n", size,
				strerror(errno));
		return false;
	}

	draw_buffer->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (draw_buffer->data == MAP_FAILED) {
		draw_buffer->data = NULL;
		fprintf(stderr, "mmap failed: %s\n", strerror(errno));
		close(fd);
		return false;
	}
	draw_buffer->size = size;

	struct wl_shm_pool *pool = wl_shm_create_pool(panel->surface.shm, fd, size);
	draw_buffer->buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
		stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);


	draw_buffer->panel = panel;
	draw_buffer->width = width;
	draw_buffer->height = height;
	wl_buffer_add_listener(draw_buffer->buffer, &buffer_listener, draw_buffer);

	cairo_surface_t *s = cairo_image_surface_create_for_data(draw_buffer->data,
															 CAIRO_FORMAT_ARGB32,
															 width, height, width * 4);
	draw_buffer->cairo = cairo_create(s);
	draw_buffer->cairo_surface = s;
	cairo_set_antialias(draw_buffer->cairo, CAIRO_ANTIALIAS_BEST);
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
	cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_SUBPIXEL);
	cairo_font_options_set_subpixel_order(fo, to_cairo_subpixel_order(m->subpixel));
	cairo_set_font_options(draw_buffer->cairo, fo);
	cairo_font_options_destroy(fo);
	cairo_save(draw_buffer->cairo);

	return true;
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};


void dmenu_init_panel(struct dmenu_panel *panel, int32_t width, int32_t height,
		bool bar, bool bottom, bool interactive) {
	if(!setlocale(LC_CTYPE, ""))
		weprintf("no locale support\n");

	if(!(panel->display_info.display = wl_display_connect(NULL)))
		eprintf("cannot open display\n");

	if ((panel->repeat_timer = timerfd_create(CLOCK_MONOTONIC, 0)) < 0)
		eprintf("cannot create timer fd\n");

	panel->width = width;
	panel->height = height;
	panel->bar = bar;
	panel->bottom = bottom;
	panel->interactive = interactive;
	panel->configured = false;
	panel->closed = false;
	panel->redraw_pending = false;
	panel->keyboard.control = false;
	panel->on_keyevent = NULL;

	struct wl_registry *registry = wl_display_get_registry(panel->display_info.display);
	wl_registry_add_listener(registry, &registry_listener, panel);

	wl_display_roundtrip(panel->display_info.display);

	/* Second roundtrip for xdg-output. Will populate display dimensions. */
	wl_display_roundtrip(panel->display_info.display);


	panel->surface.surface = wl_compositor_create_surface(panel->display_info.compositor);

	panel->monitor = NULL;
	if (!panel->selected_monitor_name && panel->selected_monitor >= 0 &&
			panel->selected_monitor < n_monitors) {
		panel->monitor = monitors[panel->selected_monitor];
	} else {
		for (int i = 0; i < n_monitors; ++i) {
			if (monitors[i] && !strncmp(panel->selected_monitor_name,
										monitors[i]->name,
										MAX_MONITOR_NAME_LEN)) {
				panel->monitor = monitors[i];
				break;
			}
		}
	}
	if (!panel->monitor) {
		if (!panel->selected_monitor_name)
			eprintf("No monitor with index %i available.\n", panel->selected_monitor);
		else
		eprintf("No monitor with name %s available.\n", panel->selected_monitor_name);
	}
	if (!panel->monitor->logical_width || !panel->monitor->logical_height)
		eprintf("selected monitor has no logical dimensions\n");
	if (!panel->bar)
		panel->width = panel->width < panel->monitor->logical_width
			? panel->width : panel->monitor->logical_width;
	panel->height = panel->height < panel->monitor->logical_height
		? panel->height : panel->monitor->logical_height;

	if (!panel->surface.layer_shell)
		eprintf("Compositor does not implement wlr-layer-shell protocol.\n");
	panel->surface.layer_surface =
		zwlr_layer_shell_v1_get_layer_surface(panel->surface.layer_shell,
											  panel->surface.surface,
											  panel->monitor->output,
											  ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
										  "dmenu-wl");

	zwlr_layer_surface_v1_set_size(panel->surface.layer_surface,
								   bar ? 0 : panel->width, panel->height);
	if (bar)
		zwlr_layer_surface_v1_set_anchor(panel->surface.layer_surface,
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				(bottom ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
				 : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP));


	zwlr_layer_surface_v1_add_listener(panel->surface.layer_surface,
									   &layer_surface_listener, panel);
	zwlr_layer_surface_v1_set_keyboard_interactivity(panel->surface.layer_surface,
			interactive);

	wl_surface_set_buffer_scale(panel->surface.surface,
								panel->monitor->scale);
	wl_surface_commit(panel->surface.surface);
	while (!panel->configured && !panel->closed &&
			wl_display_dispatch(panel->display_info.display) >= 0)
		;
	if (panel->closed)
		eprintf("layer surface was closed by the compositor\n");
	if (!panel->configured)
		eprintf("layer surface was not configured\n");

	if (!dmenu_create_buffer(panel, &panel->surface.buffers[0]) ||
			!dmenu_create_buffer(panel, &panel->surface.buffers[1]))
		eprintf("cannot create drawing buffer\n");

}

void dmenu_show(struct dmenu_panel *dmenu) {
	dmenu_draw(dmenu);

	struct pollfd fds[] = {
		{ wl_display_get_fd(dmenu->display_info.display), POLLIN },
		{ dmenu->repeat_timer, POLLIN },
	};
	const int nfds = sizeof(fds) / sizeof(*fds);

	wl_display_flush(dmenu->display_info.display);

	dmenu->running = true;
	while (dmenu->running) {
		fds[0].events = POLLIN;
		if (wl_display_flush(dmenu->display_info.display) < 0) {
			if (errno == EAGAIN)
				fds[0].events |= POLLOUT;
			else
				break;
		}

		if (poll(fds, nfds, -1) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		if (fds[0].revents & POLLIN) {
			if (wl_display_dispatch(dmenu->display_info.display) < 0) {
				dmenu->running = false;
			}
		}

		if (fds[1].revents & POLLIN) {
			uint64_t expirations;
			if (read(dmenu->repeat_timer, &expirations, sizeof expirations) < 0 &&
					errno != EAGAIN)
				break;
			keyboard_repeat(dmenu);
		}
	}

	/* dmenu_close called */
	wl_display_disconnect(dmenu->display_info.display);

}
void dmenu_close(struct dmenu_panel *dmenu) {
	dmenu->running = false;
}


void
weprintf(const char *fmt, ...) {
	va_list ap;

	fprintf(stderr, "%s: warning: ", progname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}
