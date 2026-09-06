#include <stdint.h>

static uint32_t color_bg = 0x222222ff;
static uint32_t color_fg = 0xbbbbbbff;
static uint32_t color_input_bg = 0x222222ff;
static uint32_t color_input_fg = 0xbbbbbbff;
static uint32_t color_prompt_bg = 0x222222ff;
static uint32_t color_prompt_fg = 0xbbbbbbff;
static uint32_t color_selected_bg = 0x005577ff;
static uint32_t color_selected_fg = 0xeeeeeeff;
static uint32_t color_border = 0x005577ff;

static int32_t panel_height = 0;
static int32_t min_width = 0;
static int32_t border_width = 0;

static enum dmenu_position position = DMENU_POSITION_TOP;

static char *font = "monospace:size=10";

static int lines = 0;
