/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "draw.h"

#define INRECT(x,y,rx,ry,rw,rh) ((x) >= (rx) && (x) < (rx)+(rw) && (y) >= (ry) && (y) < (ry)+(rh))
#define MIN(a,b)                ((a) < (b) ? (a) : (b))
#define MAX(a,b)                ((a) > (b) ? (a) : (b))

typedef struct Item Item;
struct Item {
	char *text;
	Item *next;          /* traverses all items */
	Item *left, *right;  /* traverses matching items */
	int32_t width;
};

typedef enum {
    LEFT,
    RIGHT,
    CENTRE
} TextPosition;

struct {
	int32_t width;
	int32_t height;
	int32_t text_height;
	int32_t text_y;
	int32_t input_field;
	int32_t scroll_left;
	int32_t matches;
	int32_t scroll_right;
} window_config;

const char *progname;

static uint32_t color_bg = 0x000000f2;
static uint32_t color_fg = 0xffffffff;
static uint32_t color_input_bg = 0x000000ff;
static uint32_t color_input_fg = 0xffffffff;
static uint32_t color_prompt_bg = 0x000000f2;
static uint32_t color_prompt_fg = 0xffffffff;
static uint32_t color_selected_bg = 0xffc87fff;
static uint32_t color_selected_fg = 0x000000f2;
static uint32_t color_border = 0xffc87fff;

static int32_t panel_height = 20;
static int32_t min_width = 600;
static int32_t border_width = 3;

static void appenditem(Item *item, Item **list, Item **last);
static char *fstrstr(const char *s, const char *sub);
static void insert(const char *s, ssize_t n);
static void match(void);
static void calcoffsets(void);
static void measure_layout(int32_t *, int32_t *);
static size_t nextrune(int incr);
static void readstdin(void);
static void alarmhandler(int signum);
/* static void handle_return(char* value); */
static void usage(void);
static int retcode = EXIT_SUCCESS;
static int selected_monitor = 0;
static char *selected_monitor_name = 0;

static char text[BUFSIZ];
static char text_[BUFSIZ];
static int itemcount = 0;
static int lines = 20;
static int timeout = 3;
static size_t cursor = 0;
static const char *prompt = NULL;
static bool message = false;
static bool nostdin = false;
static bool returnearly = false;
static bool show_in_bottom = false;
static bool show_as_bar = false;
static bool done = false;
static TextPosition messageposition = LEFT;
static Item *items = NULL;
static Item *matches, *sel;
static Item *prev, *curr, *next;
static Item *leftmost, *rightmost;
static Item *page_start;
static struct dmenu_panel *active_panel;
static char *font = "Terminus 14";

static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;

void
insert(const char *s, ssize_t n) {
	size_t len = strlen(text);
	if ((n > 0 && (size_t)n > sizeof text - 1 - len) ||
			(n < 0 && (size_t)-n > cursor))
		return;
	memmove(text + cursor + n, text + cursor, len - cursor + 1);
	if(n > 0)
		memcpy(text + cursor, s, n);
	cursor += n;
	match();
}

void keyrepeat(struct dmenu_panel *panel) {
	if (panel->on_keyevent) {
		panel->on_keyevent(panel, panel->repeat_key_state, panel->repeat_sym,
						   panel->keyboard.control, panel->keyboard.shift);
	}
}

void keypress(struct dmenu_panel *panel, enum wl_keyboard_key_state state,
			  xkb_keysym_t sym, bool ctrl, bool shft) {
	char buf[8];
	size_t len = strlen(text);

	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;

	if (ctrl) {
		switch (xkb_keysym_to_lower(sym)) {
		case XKB_KEY_a:
			sym = XKB_KEY_Home;
			break;
		case XKB_KEY_e:
			sym = XKB_KEY_End;
			break;
		case XKB_KEY_f:
			sym = XKB_KEY_Right;
			break;
		case XKB_KEY_n:
			sym = lines ? XKB_KEY_Down : XKB_KEY_Right;
			break;
		case XKB_KEY_b:
			sym = XKB_KEY_Left;
			break;
		case XKB_KEY_p:
			sym = lines ? XKB_KEY_Up : XKB_KEY_Left;
			break;
		case XKB_KEY_h:
			sym = XKB_KEY_BackSpace;
			break;
		case XKB_KEY_d:
			sym = XKB_KEY_Delete;
			break;
		case XKB_KEY_k:
			sym = XKB_KEY_Up;
			break;
		case XKB_KEY_u:
			memmove(text, text + cursor, len - cursor + 1);
			cursor = 0;
			match();
			dmenu_draw(panel);
			return;
		case XKB_KEY_w:
			if (cursor) {
				size_t start = cursor;
				while (start && text[start - 1] == ' ')
					start--;
				while (start && text[start - 1] != ' ')
					start--;
				insert(NULL, (ssize_t)start - (ssize_t)cursor);
			}
			dmenu_draw(panel);
			return;
		case XKB_KEY_j:
			sym = XKB_KEY_Down;
			break;
		case XKB_KEY_g:
		case XKB_KEY_c:
			retcode = EXIT_FAILURE;
			dmenu_close(panel);
			return;
		}
	}
	switch (sym) {
	case XKB_KEY_KP_Enter: /* fallthrough */
	case XKB_KEY_Return:
		puts((sel && !shft) ? sel->text : text);
		fflush(stdout);
		if (!ctrl)
			dmenu_close(panel);
		break;
	case XKB_KEY_Escape:
		retcode = EXIT_FAILURE;
		dmenu_close(panel);
		break;
	case XKB_KEY_Left:
		if(cursor && (!sel || !sel->left)) {
			cursor = nextrune(-1);
		} if (sel && sel->left) {
			sel = sel->left;
			if (!lines && leftmost && sel == leftmost->left)
				leftmost = sel;
		}
		break;
	case XKB_KEY_Right:
		if (cursor < len) {
			cursor = nextrune(+1);
		} else if (cursor == len) {
			if (sel && sel->right) sel = sel->right;
			if (!lines && rightmost && sel == rightmost->right)
				leftmost = sel;
		}
		break;
	case XKB_KEY_Up:
		if (sel && sel->left)
			sel = sel->left;
		break;
	case XKB_KEY_Down:
		if (sel && sel->right)
			sel = sel->right;
		break;
	case XKB_KEY_Page_Up:
		for (int i = 0; sel && sel->left && i < MAX(lines, 1); i++)
			sel = sel->left;
		break;
	case XKB_KEY_Page_Down:
		for (int i = 0; sel && sel->right && i < MAX(lines, 1); i++)
			sel = sel->right;
		page_start = sel;
		break;

	case XKB_KEY_End:
		if(cursor < len) {
			cursor = len;
			break;
		}
		while(sel && sel->right)
			sel = sel->right;
		break;
	case XKB_KEY_Home:
		if(sel == matches) {
			cursor = 0;
			break;
		}
		sel = curr = matches;
		leftmost = matches;
		/* calcoffsets(); */
		break;

	case XKB_KEY_BackSpace:
		if (cursor > 0)
			insert(NULL, nextrune(-1) - cursor);
		break;
	case XKB_KEY_Delete:
		if (cursor == len)
			return;
		{
			size_t next_cursor = nextrune(+1);
			memmove(text + cursor, text + next_cursor, len - next_cursor + 1);
			match();
		}
		break;
	case XKB_KEY_Tab:
		if(!sel) return;
		strncpy(text, sel->text, sizeof text - 1);
		text[sizeof text - 1] = '\0';
		cursor = strlen(text);
		match();
		break;
	default:
		if (xkb_keysym_to_utf8(sym, buf, 8)) {
			insert(buf, strnlen(buf, 8));
		}
	}
	calcoffsets();
	dmenu_draw(panel);
}

void cairo_set_source_u32(cairo_t *cairo, uint32_t color) {
	cairo_set_source_rgba(cairo,
			(color >> (3*8) & 0xFF) / 255.0,
			(color >> (2*8) & 0xFF) / 255.0,
			(color >> (1*8) & 0xFF) / 255.0,
			(color >> (0*8) & 0xFF) / 255.0);
}

int32_t draw_text(cairo_t *cairo, int32_t width, int32_t height, const char *str,
				  int32_t x, int32_t scale, uint32_t
				  foreground_color, uint32_t background_color, int32_t padding) {

	int32_t text_width, text_height;
	get_text_size(cairo, font, &text_width, &text_height,
				  NULL, scale, false, "%s", str);
	int32_t text_y = (height / 2.0) - (text_height / 2.0);

	if (x + padding * scale + text_width + 30 * scale > width) {
		cairo_save(cairo);
		cairo_rectangle(cairo, x, 0, MAX(0, width - x), height);
		cairo_clip(cairo);
		if (background_color) {
			cairo_set_source_u32(cairo, background_color);
			cairo_paint(cairo);
		}
		cairo_move_to(cairo, x + padding * scale, text_y);
		cairo_set_source_u32(cairo, foreground_color);
		pango_printf(cairo, font, scale, false, "%s", str);
		cairo_restore(cairo);
		return width;
	} else {
		if (background_color) {
			cairo_set_source_u32(cairo, background_color);
			cairo_rectangle(cairo, x, 0, text_width + 2 * padding * scale, height);
			cairo_fill(cairo);
		}

		cairo_move_to(cairo, x + padding * scale, text_y);
		cairo_set_source_u32(cairo, foreground_color);

		pango_printf(cairo, font, scale, false, "%s", str);
	}

	return x + text_width + 2 * padding * scale;
}

static void
draw_content(cairo_t *cairo, int32_t width, int32_t height, int32_t scale) {
	int32_t row_height = panel_height * scale;
	int32_t x = 0;
	int32_t item_padding = 10;

	int32_t text_width, text_height;
	get_text_size(cairo, font, &text_width, &text_height, NULL, scale,
				  false, "Aj");
	int32_t text_y = (row_height - text_height) / 2;

	cairo_set_source_u32(cairo, color_bg);
	cairo_paint(cairo);
	if (message) {
		const char *value = items ? items->text : "";
		get_text_size(cairo, font, &text_width, NULL, NULL, scale, false,
				"%s", value);
		switch (messageposition) {
		case CENTRE:
			x = MAX(0, (width - text_width) / 2);
			break;
		case RIGHT:
			x = MAX(0, width - text_width - 10 * scale);
			break;
		default:
			x = 10 * scale;
		}
		cairo_save(cairo);
		cairo_rectangle(cairo, 0, 0, width, row_height);
		cairo_clip(cairo);
		cairo_move_to(cairo, x, text_y);
		cairo_set_source_u32(cairo, color_fg);
		pango_printf(cairo, font, scale, false, "%s", value);
		cairo_restore(cairo);
		return;
	}

	if (prompt) {
		x = draw_text(cairo, width, row_height, prompt, 0, scale, color_prompt_fg,
					  color_prompt_bg, 6);
	}
	window_config.input_field = x;

	int32_t input_end = lines ? width
		: MIN(width, MAX(width / 3, x + 300 * scale));
	cairo_set_source_u32(cairo, color_input_bg);
	cairo_rectangle(cairo, x, 0, input_end - x, row_height);
	cairo_fill(cairo);

	cairo_save(cairo);
	cairo_rectangle(cairo, x, 0, input_end - x, row_height);
	cairo_clip(cairo);
	draw_text(cairo, input_end, row_height, text, x, scale, color_input_fg, 0, 6);

	{
		/* draw cursor */
		memset(text_, 0, BUFSIZ);
		strncpy(text_, text, cursor);
		int32_t text_width, text_height;
		get_text_size(cairo, font, &text_width, &text_height, NULL, scale,
					  false, "%s", text_);
		/* int32_t text_y = (height / 2.0) - (text_height / 2.0); */
		int32_t padding = 6 * scale;
		cairo_rectangle(cairo, x + padding + text_width, text_y,
						scale, text_height);
		cairo_fill(cairo);
	}
	cairo_restore(cairo);

	if (lines > 0) {
		int row = 1;
		for (Item *item = page_start; item && row <= lines; item = item->right, row++) {
			uint32_t bg = item == sel ? color_selected_bg : color_bg;
			uint32_t fg = item == sel ? color_selected_fg : color_fg;
			int32_t y = row * row_height;
			cairo_save(cairo);
			cairo_rectangle(cairo, 0, y, width, row_height);
			cairo_clip(cairo);
			cairo_set_source_u32(cairo, bg);
			cairo_paint(cairo);
			cairo_move_to(cairo, item_padding * scale,
					y + (row_height - text_height) / 2);
			cairo_set_source_u32(cairo, fg);
			pango_printf(cairo, font, scale, false, "%s", item->text);
			cairo_restore(cairo);
		}
		return;
	}

	x = input_end;

	/* Scroll indicator will be drawn later if required. */
	int32_t scroll_indicator_pos = x;
	x += 20 * scale;

	if (matches) {
		/* draw matches */
		Item *item;
		/* for (item = matches; item; item = item->right) { */
		/* 	if (item->width == -1) { */
		/* 		get_text_size(cairo, font, &item->width, NULL, NULL, scale, */
		/* 					  false, item->text); */
		/* 		item->width += item_padding; */
		/* 		/\* printf("%d ", item->width); *\/ */
		/* 	} */
		/* } */

		/* /\* Figure out if we need to scroll. *\/ */
		/* int32_t item_pos = x; */
		/* bool found = false; */
		/* rightmost = NULL; */
		/* for (item = leftmost; item; item = item->right) { */
		/* 	item_pos += item->width; */
		/* 	if (item_pos >= (width - x - 80 * scale)) { */
		/* 		rightmost = item->left; */
		/* 		printf("rightmost: %s\n", item->left->text); */
		/* 		found = true; */
		/* 		break; */
		/* 	} */
		/* } */

		rightmost = NULL;
		for (item = leftmost; item; item = item->right) {
			uint32_t bg_color = sel == item ? color_selected_bg : color_bg;
			uint32_t fg_color = sel == item ? color_selected_fg : color_fg;
			if (x < width) {
				/* x = draw_text(cairo, width - 20 * scale, height, item->text, */
				/* 			  x, scale, fg_color, bg_color, item_padding); */
				x = draw_text(cairo, width - 20 * scale, row_height, item->text,
							  x, scale, fg_color, bg_color, item_padding);
			} else {
				break;
			}
			rightmost = item;
		}

		if (leftmost != matches) {
			cairo_move_to(cairo, scroll_indicator_pos, text_y);
			pango_printf(cairo, font, scale, false, "<");
		}
		if (rightmost && rightmost->right) {
			cairo_move_to(cairo, width - 12 * scale, text_y);
			cairo_set_source_u32(cairo, color_fg);
			pango_printf(cairo, font, scale, false, ">");
		}
	}
}

void
draw(cairo_t *cairo, int32_t width, int32_t height, int32_t scale) {
	int32_t max_border = (MIN(width, height) - 1) / 2;
	int64_t scaled_border = (int64_t)border_width * scale;
	int32_t border = scaled_border < max_border
		? scaled_border : MAX(0, max_border);

	cairo_set_source_u32(cairo, color_border);
	cairo_paint(cairo);
	if (width <= 2 * border || height <= 2 * border)
		return;

	cairo_save(cairo);
	cairo_rectangle(cairo, border, border,
			width - 2 * border, height - 2 * border);
	cairo_clip(cairo);
	cairo_translate(cairo, border, border);
	draw_content(cairo, width - 2 * border, height - 2 * border, scale);
	cairo_restore(cairo);
}

uint32_t parse_color(char *str) {
	if (!str) eprintf("NULL as color value\n");

	size_t len = strnlen(str, BUFSIZ);

	if ((len != 7 && len != 9) || str[0] != '#')
		eprintf("Color format must be '#rrggbb[aa]'\n");

	uint32_t _val = strtol(&str[1], NULL, 16);

	uint32_t color = 0x000000ff;
	if (len == 9) /* Alpha specified */
		color = _val;
	else /* No alpha specified, assume full opacity */
		color = (_val << 8) + 0xff;

	return color;
}

static int32_t
parse_nonnegative(const char *option, const char *value) {
	char *end;
	long result = strtol(value, &end, 10);
	if (!value[0] || *end || result < 0 || result > INT32_MAX)
		eprintf("invalid value for %s: %s\n", option, value);
	return result;
}

static void
measure_layout(int32_t *width, int32_t *height) {
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t *cairo = cairo_create(surface);
	int32_t widest = 0, prompt_width = 0, text_height = 0;

	get_text_size(cairo, font, NULL, &text_height, NULL, 1, false, "Aj");
	panel_height = MAX(panel_height, text_height + 2);
	if (prompt) {
		get_text_size(cairo, font, &prompt_width, NULL, NULL, 1, false, "%s", prompt);
		prompt_width += 12;
	}
	for (Item *item = items; item; item = item->next) {
		get_text_size(cairo, font, &item->width, NULL, NULL, 1, false, "%s", item->text);
		widest = MAX(widest, item->width + 20);
	}
	int64_t natural_width = (int64_t)prompt_width + widest + 2 * border_width;
	int64_t rows = 1 + (lines ? MIN(lines, itemcount) : 0);
	int64_t requested_width = MAX((int64_t)min_width, natural_width);
	int64_t requested_height = (int64_t)panel_height * rows + 2 * border_width;
	if (requested_width <= 0 || requested_width > INT32_MAX ||
			requested_height <= 0 || requested_height > INT32_MAX)
		eprintf("requested menu dimensions are too large\n");
	*width = requested_width;
	*height = requested_height;
	cairo_destroy(cairo);
	cairo_surface_destroy(surface);
}

int
main(int argc, char **argv) {
  int i;

  progname = "dmenu";
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
      fputs("dmenu-wl-" VERSION
            ", © 2006-2018 dmenu engineers, see LICENSE for details\n",
            stdout);
      exit(EXIT_SUCCESS);
    } else if (!strcmp(argv[i], "-b") || !strcmp(argv[i], "--bottom"))
	  show_as_bar = show_in_bottom = true;
    else if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--top"))
	  show_as_bar = true, show_in_bottom = false;
    else if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "--echo"))
      message = true;
    else if (!strcmp(argv[i], "-ec") || !strcmp(argv[i], "--echo-centre"))
      message = true, messageposition = CENTRE;
    else if (!strcmp(argv[i], "-er") || !strcmp(argv[i], "--echo-right"))
      message = true, messageposition = RIGHT;
    else if (!strcmp(argv[i], "-i") || !strcmp(argv[i], "--insensitive"))
      fstrncmp = strncasecmp;
    else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--return-early"))
      returnearly = true;
    else if (i == argc - 1) {
      usage();

    }
    /* opts that need 1 arg */
	else if (!strcmp(argv[i], "-et") || !strcmp(argv[i], "--echo-timeout"))
	  timeout = parse_nonnegative("--echo-timeout", argv[++i]);
	else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--height"))
	  panel_height = parse_nonnegative("--height", argv[++i]);
	else if (!strcmp(argv[i], "-l") || !strcmp(argv[i], "--lines"))
	  lines = parse_nonnegative("--lines", argv[++i]);
	else if (!strcmp(argv[i], "-mw") || !strcmp(argv[i], "--min-width"))
	  min_width = parse_nonnegative("--min-width", argv[++i]);
	else if (!strcmp(argv[i], "-bw") || !strcmp(argv[i], "--border-width"))
	  border_width = parse_nonnegative("--border-width", argv[++i]);
    else if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--monitor")) {
		++i;
		bool is_num = true;
		for (int j = 0; j < strlen(argv[i]); ++j) {
			if (!isdigit(argv[i][j])) {
				is_num = false;
				break;
			}
		}
		if (is_num) {
			selected_monitor = atoi(argv[i]);
		} else {
			selected_monitor = -1;
			selected_monitor_name = argv[i];
		}
	}
    else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--prompt"))
      prompt = argv[++i];
    else if (!strcmp(argv[i], "-po") || !strcmp(argv[i], "--prompt-only"))
      prompt = argv[++i], nostdin = true;
    else if (!strcmp(argv[i], "-fn") || !strcmp(argv[i], "--font-name"))
      font = argv[++i];
    else if (!strcmp(argv[i], "-nb") || !strcmp(argv[i], "--normal-background"))
      color_bg = color_input_bg = parse_color(argv[++i]);
    else if (!strcmp(argv[i], "-nf") || !strcmp(argv[i], "--normal-foreground"))
      color_fg = color_input_fg = parse_color(argv[++i]);
    else if (!strcmp(argv[i], "-sb") ||
             !strcmp(argv[i], "--selected-background"))
      color_prompt_bg = color_selected_bg = parse_color(argv[++i]);
    else if (!strcmp(argv[i], "-sf") ||
             !strcmp(argv[i], "--selected-foreground"))
      color_prompt_fg = color_selected_fg = parse_color(argv[++i]);
	else if (!strcmp(argv[i], "-bc") || !strcmp(argv[i], "--border-color"))
	  color_border = parse_color(argv[++i]);
    else {
      usage();
    }
  }

    if (message) {
        signal(SIGALRM, alarmhandler);
        alarm(timeout);
    }
    if(!nostdin) {
        readstdin();
    }

	int32_t menu_width, menu_height;
	measure_layout(&menu_width, &menu_height);

	struct dmenu_panel dmenu = {0};
	dmenu.selected_monitor = selected_monitor;
	dmenu.selected_monitor_name = selected_monitor_name;
	dmenu_init_panel(&dmenu, menu_width, menu_height, show_as_bar, show_in_bottom,
			!message);


	dmenu.on_keyevent = keypress;
	dmenu.on_keyrepeat = keyrepeat;
	dmenu.draw = draw;
	active_panel = &dmenu;
	match();

	if (!done)
		dmenu_show(&dmenu);

	return retcode;
}

void
appenditem(Item *item, Item **list, Item **last) {
	if(!*last)
		*list = item;
	else
		(*last)->right = item;
	item->left = *last;
	item->right = NULL;
	*last = item;
}

static void
calcoffsets(void) {
	if (!lines || !sel) {
		page_start = matches;
		return;
	}
	if (!page_start)
		page_start = matches;

	Item *item = page_start;
	for (int i = 0; item && i < lines; i++, item = item->right)
		if (item == sel)
			return;

	if (sel->left) {
		item = sel;
		for (int i = 1; item->left && i < lines; i++)
			item = item->left;
		page_start = item;
	} else {
		page_start = sel;
	}
}

char *
fstrstr(const char *s, const char *sub) {
	size_t len;

	for(len = strlen(sub); *s; s++)
		if(!fstrncmp(s, sub, len))
			return (char *)s;
	return NULL;
}

void
match(void) {
	size_t len;
	Item *item, *itemend, *lexact, *lprefix, *lsubstr, *exactend, *prefixend, *substrend;
	char *query = strdup(text);
	char *tokens[BUFSIZ / 2 + 1];
	size_t token_count = 0;
	char *token;

	if (!query)
		eprintf("cannot duplicate query\n");
	for (token = strtok(query, " "); token && token_count < BUFSIZ / 2;
			token = strtok(NULL, " "))
		tokens[token_count++] = token;

	rightmost = leftmost = NULL;
	len = strlen(text);
	matches = lexact = lprefix = lsubstr = itemend = exactend = prefixend = substrend = NULL;
	for(item = items; item; item = item->next) {
		bool token_match = true;
		for (size_t i = 0; i < token_count; i++)
			if (!fstrstr(item->text, tokens[i])) {
				token_match = false;
				break;
			}
		if (!token_match)
			continue;
		if(!fstrncmp(text, item->text, len + 1)) {
			appenditem(item, &lexact, &exactend);
        }
		else if(token_count && !fstrncmp(tokens[0], item->text, strlen(tokens[0]))) {
			appenditem(item, &lprefix, &prefixend);
        }
		else {
			appenditem(item, &lsubstr, &substrend);
        }
	}
	free(query);

	if(lexact) {
		matches = lexact;
		itemend = exactend;
	}
	if(lprefix) {
		if(itemend) {
			itemend->right = lprefix;
			lprefix->left = itemend;
		}
		else
			matches = lprefix;
		itemend = prefixend;
	}
	if(lsubstr) {
		if(itemend) {
			itemend->right = lsubstr;
			lsubstr->left = itemend;
		}
		else
			matches = lsubstr;
	}
	curr = prev = next = sel = matches;
	page_start = matches;
	calcoffsets();

	leftmost = matches;

	if(returnearly && curr && !curr->right && active_panel) {
		puts(curr->text);
		fflush(stdout);
		done = true;
		dmenu_close(active_panel);
	}
}

size_t
nextrune(int incr) {
	size_t n, len;

	len = strlen(text);
	for(n = cursor + incr; n >= 0 && n < len && (text[n] & 0xc0) == 0x80; n += incr);
	return n;
}

void
readstdin(void) {
	char buf[sizeof text], *p;
	Item *item, **end;

	for(end = &items; fgets(buf, sizeof buf, stdin); *end = item, end = &item->next) {
        itemcount++;

		if((p = strchr(buf, '\n'))) {
			*p = '\0';
        }
		if(!(item = malloc(sizeof *item))) {
			eprintf("cannot malloc %u bytes\n", sizeof *item);
        }
		item->width = -1;
		if(!(item->text = strdup(buf))) {
			eprintf("cannot strdup %u bytes\n", strlen(buf)+1);
        }
		item->next = item->left = item->right = NULL;
		/* inputw = MAX(inputw, textw(dc, item->text)); */
	}
}


void
alarmhandler(int signum) {
    exit(EXIT_SUCCESS);
}

void
usage(void) {
    printf("Usage: dmenu [OPTION]...\n");
	printf("Display newline-separated input stdin as a menu\n");
    printf("\n");
    printf("  -e,  --echo                       display text from stdin with no user\n");
    printf("                                      interaction\n");
    printf("  -ec, --echo-centre                same as -e but align text centrally\n");
    printf("  -er, --echo-right                 same as -e but align text right\n");
    printf("  -et, --echo-timeout SECS          close the message after SEC seconds\n");
    printf("                                      when using -e, -ec, or -er\n");
	printf("  -b,  --bottom                     dmenu appears at the bottom of the screen\n");
	printf("  -t,  --top                        dmenu appears as a top bar\n");
	printf("  -h,  --height N                   set dmenu to be N pixels high\n");
    printf("  -i,  --insensitive                dmenu matches menu items case insensitively\n");
    printf("  -l,  --lines LINES                dmenu lists items vertically, within the\n");
	printf("                                      given number of lines\n");
	printf("  -mw, --min-width N                minimum floating width in pixels\n");
	printf("  -bw, --border-width N             border width in logical pixels\n");
	printf("  -bc, --border-color COLOR         border color (#RRGGBB or #RRGGBBAA)\n");
	printf("  -m,  --monitor MONITOR            output index or name on which to appear\n");
    printf("  -p,  --prompt  PROMPT             prompt to be displayed to the left of the\n");
    printf("                                      input field\n");
    printf("  -po, --prompt-only  PROMPT        same as -p but don't wait for stdin\n");
    printf("                                      useful for a prompt with no menu\n");
    printf("  -r,  --return-early               return as soon as a single match is found\n");
    printf("  -fn, --font-name FONT             font or font set to be used\n");
    printf("  -nb, --normal-background COLOR    normal background color\n");
    printf("                                      #RRGGBB and #RRGGBBAA supported\n");
    printf("  -nf, --normal-foreground COLOR    normal foreground color\n");
    printf("  -sb, --selected-background COLOR  selected background color\n");
    printf("  -sf, --selected-foreground COLOR  selected foreground color\n");
    printf("  -v,  --version                    display version information\n");

	exit(EXIT_FAILURE);
}
