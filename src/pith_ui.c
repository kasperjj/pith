/*
 * pith_ui.c - Raylib-based UI renderer for Pith
 * 
 * This is the platform-specific rendering layer. It renders View trees
 * produced by the runtime and captures user input as events.
 */

#include "pith_ui.h"
#include "raylib.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ========================================================================
   UI STATE
   ======================================================================== */

struct PithUI {
    PithUIConfig config;
    Font font;
    bool font_loaded;
    
    /* Window size in cells */
    int cells_wide;
    int cells_high;
    
    /* Input state */
    int last_key;
    bool key_pending;
};

/* ========================================================================
   CONFIGURATION
   ======================================================================== */

PithUIConfig pith_ui_default_config(void) {
    return (PithUIConfig){
        .window_width = 1200,
        .window_height = 800,
        .title = "Pith",
        
        .cell_width = 10,
        .cell_height = 20,
        
        .font_path = NULL,
        .font_size = 18,
        
        .color_fg = PITH_COLOR_WHITE,
        .color_bg = PITH_COLOR_BLACK,
        .color_border = PITH_COLOR_GRAY,
        .color_selection = PITH_COLOR_BLUE,
    };
}

/* ========================================================================
   COLOR HELPERS
   ======================================================================== */

static Color rgba_to_color(uint32_t rgba) {
    return (Color){
        .r = (rgba >> 24) & 0xFF,
        .g = (rgba >> 16) & 0xFF,
        .b = (rgba >> 8) & 0xFF,
        .a = rgba & 0xFF,
    };
}

/* Open Color palette - https://yeun.github.io/open-color/ */
typedef struct {
    const char *name;
    uint32_t shades[10];  /* shades 0-9 */
} OpenColorFamily;

static const OpenColorFamily open_colors[] = {
    {"gray", {
        0xf8f9faff, 0xf1f3f5ff, 0xe9ecefff, 0xdee2e6ff, 0xced4daff,
        0xadb5bdff, 0x868e96ff, 0x495057ff, 0x343a40ff, 0x212529ff
    }},
    {"red", {
        0xfff5f5ff, 0xffe3e3ff, 0xffc9c9ff, 0xffa8a8ff, 0xff8787ff,
        0xff6b6bff, 0xfa5252ff, 0xf03e3eff, 0xe03131ff, 0xc92a2aff
    }},
    {"pink", {
        0xfff0f6ff, 0xffdeebff, 0xfcc2d7ff, 0xfaa2c1ff, 0xf783acff,
        0xf06595ff, 0xe64980ff, 0xd6336cff, 0xc2255cff, 0xa61e4dff
    }},
    {"grape", {
        0xf8f0fcff, 0xf3d9faff, 0xeebefaff, 0xe599f7ff, 0xda77f2ff,
        0xcc5de8ff, 0xbe4bdbff, 0xae3ec9ff, 0x9c36b5ff, 0x862e9cff
    }},
    {"violet", {
        0xf3f0ffff, 0xe5dbffff, 0xd0bfffff, 0xb197fcff, 0x9775faff,
        0x845ef7ff, 0x7950f2ff, 0x7048e8ff, 0x6741d9ff, 0x5f3dc4ff
    }},
    {"indigo", {
        0xedf2ffff, 0xdbe4ffff, 0xbac8ffff, 0x91a7ffff, 0x748ffcff,
        0x5c7cfaff, 0x4c6ef5ff, 0x4263ebff, 0x3b5bdbff, 0x364fc7ff
    }},
    {"blue", {
        0xe7f5ffff, 0xd0ebffff, 0xa5d8ffff, 0x74c0fcff, 0x4dabf7ff,
        0x339af0ff, 0x228be6ff, 0x1c7ed6ff, 0x1971c2ff, 0x1864abff
    }},
    {"cyan", {
        0xe3fafcff, 0xc5f6faff, 0x99e9f2ff, 0x66d9e8ff, 0x3bc9dbff,
        0x22b8cfff, 0x15aabfff, 0x1098adff, 0x0c8599ff, 0x0b7285ff
    }},
    {"teal", {
        0xe6fcf5ff, 0xc3fae8ff, 0x96f2d7ff, 0x63e6beff, 0x38d9a9ff,
        0x20c997ff, 0x12b886ff, 0x0ca678ff, 0x099268ff, 0x087f5bff
    }},
    {"green", {
        0xebfbeeff, 0xd3f9d8ff, 0xb2f2bbff, 0x8ce99aff, 0x69db7cff,
        0x51cf66ff, 0x40c057ff, 0x37b24dff, 0x2f9e44ff, 0x2b8a3eff
    }},
    {"lime", {
        0xf4fce3ff, 0xe9fac8ff, 0xd8f5a2ff, 0xc0eb75ff, 0xa9e34bff,
        0x94d82dff, 0x82c91eff, 0x74b816ff, 0x66a80fff, 0x5c940dff
    }},
    {"yellow", {
        0xfff9dbff, 0xfff3bfff, 0xffec99ff, 0xffe066ff, 0xffd43bff,
        0xfcc419ff, 0xfab005ff, 0xf59f00ff, 0xf08c00ff, 0xe67700ff
    }},
    {"orange", {
        0xfff4e6ff, 0xffe8ccff, 0xffd8a8ff, 0xffc078ff, 0xffa94dff,
        0xff922bff, 0xfd7e14ff, 0xf76707ff, 0xe8590cff, 0xd9480fff
    }},
    {NULL, {0}}
};

static uint32_t lookup_open_color(const char *str) {
    /* Check for "colorname N" format (e.g., "red 5") */
    char name[32];
    int shade = -1;

    /* Try to parse "name N" format */
    if (sscanf(str, "%31s %d", name, &shade) == 2) {
        if (shade < 0 || shade > 9) shade = 6;
    } else {
        /* Just a name, default to shade 6 */
        strncpy(name, str, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        shade = 6;
    }

    /* Convert to lowercase for comparison */
    for (char *p = name; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p += 32;
    }

    /* Look up in Open Color palette */
    for (int i = 0; open_colors[i].name != NULL; i++) {
        if (strcmp(name, open_colors[i].name) == 0) {
            return open_colors[i].shades[shade];
        }
    }

    return 0; /* Not found */
}

uint32_t pith_color_parse(const char *str) {
    if (!str) return PITH_COLOR_WHITE;

    /* Special colors */
    if (strcmp(str, "black") == 0) return PITH_COLOR_BLACK;
    if (strcmp(str, "white") == 0) return PITH_COLOR_WHITE;

    /* Hex color #RRGGBB or #RRGGBBAA */
    if (str[0] == '#') {
        unsigned int r, g, b, a = 255;
        if (strlen(str) == 7) {
            sscanf(str, "#%02x%02x%02x", &r, &g, &b);
        } else if (strlen(str) == 9) {
            sscanf(str, "#%02x%02x%02x%02x", &r, &g, &b, &a);
        }
        return (r << 24) | (g << 16) | (b << 8) | a;
    }

    /* Open Color palette lookup */
    uint32_t oc = lookup_open_color(str);
    if (oc != 0) return oc;

    /* Legacy named colors (map to Open Color equivalents) */
    if (strcmp(str, "red") == 0) return lookup_open_color("red 6");
    if (strcmp(str, "green") == 0) return lookup_open_color("green 6");
    if (strcmp(str, "blue") == 0) return lookup_open_color("blue 6");
    if (strcmp(str, "yellow") == 0) return lookup_open_color("yellow 6");
    if (strcmp(str, "cyan") == 0) return lookup_open_color("cyan 6");
    if (strcmp(str, "magenta") == 0) return lookup_open_color("grape 6");
    if (strcmp(str, "gray") == 0) return lookup_open_color("gray 6");
    if (strcmp(str, "darkgray") == 0) return lookup_open_color("gray 8");

    return PITH_COLOR_WHITE;
}

/* ========================================================================
   UI LIFECYCLE
   ======================================================================== */

PithUI* pith_ui_new(PithUIConfig config) {
    PithUI *ui = malloc(sizeof(PithUI));
    memset(ui, 0, sizeof(PithUI));
    ui->config = config;
    
    /* Initialize raylib */
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(config.window_width, config.window_height, config.title);
    SetTargetFPS(60);
    
    /* Load font */
    if (config.font_path) {
        ui->font = LoadFontEx(config.font_path, config.font_size, NULL, 0);
        ui->font_loaded = true;
    } else {
        ui->font = GetFontDefault();
        ui->font_loaded = false;
    }
    
    /* Calculate cell dimensions */
    ui->cells_wide = config.window_width / config.cell_width;
    ui->cells_high = config.window_height / config.cell_height;
    
    return ui;
}

void pith_ui_free(PithUI *ui) {
    if (!ui) return;
    
    if (ui->font_loaded) {
        UnloadFont(ui->font);
    }
    
    CloseWindow();
    free(ui);
}

bool pith_ui_should_close(PithUI *ui) {
    return WindowShouldClose();
}

/* ========================================================================
   FRAME MANAGEMENT
   ======================================================================== */

void pith_ui_begin_frame(PithUI *ui) {
    /* Update cell count on resize */
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    ui->cells_wide = width / ui->config.cell_width;
    ui->cells_high = height / ui->config.cell_height;
    
    BeginDrawing();
    ClearBackground(rgba_to_color(ui->config.color_bg));
}

void pith_ui_end_frame(PithUI *ui) {
    EndDrawing();
}

/* ========================================================================
   RENDERING
   ======================================================================== */

/* Forward declaration for recursive rendering */
static void render_view_internal(PithUI *ui, PithView *view, 
                                  int x, int y, int width, int height,
                                  PithStyle *inherited_style);

/* Get effective style value, checking view then inherited */
static uint32_t get_color(PithView *view, PithStyle *inherited, uint32_t default_val) {
    if (view->style.has_color) return view->style.color;
    if (inherited && inherited->has_color) return inherited->color;
    return default_val;
}

static uint32_t get_background(PithView *view, PithStyle *inherited, uint32_t default_val) {
    if (view->style.has_background) return view->style.background;
    if (inherited && inherited->has_background) return inherited->background;
    return default_val;
}

static bool get_bold(PithView *view, PithStyle *inherited) {
    if (view->style.has_bold) return view->style.bold;
    if (inherited && inherited->has_bold) return inherited->bold;
    return false;
}

static int get_padding(PithView *view, PithStyle *inherited) {
    if (view->style.has_padding) return view->style.padding;
    if (inherited && inherited->has_padding) return inherited->padding;
    return 0;
}

static int get_gap(PithView *view, PithStyle *inherited) {
    if (view->style.has_gap) return view->style.gap;
    if (inherited && inherited->has_gap) return inherited->gap;
    return 0;
}

/* Render text at cell position */
static void render_text(PithUI *ui, const char *text, int cell_x, int cell_y, 
                        uint32_t color, bool bold) {
    int px = cell_x * ui->config.cell_width;
    int py = cell_y * ui->config.cell_height;
    
    Color c = rgba_to_color(color);
    
    /* Use raylib text drawing */
    DrawTextEx(ui->font, text, (Vector2){px, py}, 
               ui->config.font_size, 1, c);
    
    /* Fake bold by drawing twice with offset */
    if (bold) {
        DrawTextEx(ui->font, text, (Vector2){px + 1, py}, 
                   ui->config.font_size, 1, c);
    }
}

/* Render a border around a cell region */
static void render_border(PithUI *ui, int x, int y, int w, int h, 
                          const char *edges, uint32_t color) {
    if (!edges) return;
    
    int px = x * ui->config.cell_width;
    int py = y * ui->config.cell_height;
    int pw = w * ui->config.cell_width;
    int ph = h * ui->config.cell_height;
    
    Color c = rgba_to_color(color);
    
    bool all = strstr(edges, "all") != NULL;
    bool top = all || strstr(edges, "top") != NULL;
    bool bottom = all || strstr(edges, "bottom") != NULL;
    bool left = all || strstr(edges, "left") != NULL;
    bool right = all || strstr(edges, "right") != NULL;
    
    if (top) DrawLine(px, py, px + pw, py, c);
    if (bottom) DrawLine(px, py + ph, px + pw, py + ph, c);
    if (left) DrawLine(px, py, px, py + ph, c);
    if (right) DrawLine(px + pw, py, px + pw, py + ph, c);
}

/* Render a filled rectangle */
static void render_rect(PithUI *ui, int x, int y, int w, int h, uint32_t color) {
    int px = x * ui->config.cell_width;
    int py = y * ui->config.cell_height;
    int pw = w * ui->config.cell_width;
    int ph = h * ui->config.cell_height;
    
    DrawRectangle(px, py, pw, ph, rgba_to_color(color));
}

/* Calculate view size in cells */
static void measure_view(PithUI *ui, PithView *view, int *out_w, int *out_h) {
    switch (view->type) {
        case VIEW_TEXT:
            /* Approximate: 1 cell per character */
            *out_w = view->as.text.content ? strlen(view->as.text.content) : 0;
            *out_h = 1;
            break;
            
        case VIEW_TEXTFIELD:
            *out_w = view->as.textfield.content ? strlen(view->as.textfield.content) : 10;
            *out_h = 1;
            break;
            
        case VIEW_BUTTON:
            *out_w = view->as.button.label ? strlen(view->as.button.label) + 4 : 6;
            *out_h = 1;
            break;
            
        case VIEW_TEXTURE:
            *out_w = 10; /* TODO: actual texture size */
            *out_h = 10;
            break;
            
        case VIEW_VSTACK: {
            int max_w = 0, total_h = 0;
            int gap = get_gap(view, NULL);
            for (size_t i = 0; i < view->as.stack.count; i++) {
                int cw, ch;
                measure_view(ui, view->as.stack.children[i], &cw, &ch);
                if (cw > max_w) max_w = cw;
                total_h += ch;
                if (i > 0) total_h += gap;
            }
            *out_w = max_w;
            *out_h = total_h;
            break;
        }
            
        case VIEW_HSTACK: {
            int total_w = 0, max_h = 0;
            int gap = get_gap(view, NULL);
            for (size_t i = 0; i < view->as.stack.count; i++) {
                int cw, ch;
                measure_view(ui, view->as.stack.children[i], &cw, &ch);
                total_w += cw;
                if (ch > max_h) max_h = ch;
                if (i > 0) total_w += gap;
            }
            *out_w = total_w;
            *out_h = max_h;
            break;
        }

        case VIEW_SPACER:
            /* Spacer has no intrinsic size - it expands to fill */
            *out_w = 0;
            *out_h = 0;
            break;
    }
    
    /* Apply explicit size constraints */
    if (view->style.has_width && view->style.width > 0) {
        *out_w = view->style.width;
    }
    if (view->style.has_height && view->style.height > 0) {
        *out_h = view->style.height;
    }
    
    /* Add padding */
    int padding = get_padding(view, NULL);
    *out_w += padding * 2;
    *out_h += padding * 2;
}

/* Internal rendering function */
static void render_view_internal(PithUI *ui, PithView *view, 
                                  int x, int y, int width, int height,
                                  PithStyle *inherited_style) {
    if (!view) return;
    
    int padding = get_padding(view, inherited_style);
    uint32_t bg = get_background(view, inherited_style, 0);
    uint32_t fg = get_color(view, inherited_style, ui->config.color_fg);
    bool bold = get_bold(view, inherited_style);
    
    /* Draw background if set */
    if (view->style.has_background) {
        render_rect(ui, x, y, width, height, bg);
    }
    
    /* Draw border if set */
    if (view->style.has_border && view->style.border) {
        render_border(ui, x, y, width, height, 
                      view->style.border, ui->config.color_border);
    }
    
    /* Adjust for padding */
    int inner_x = x + padding;
    int inner_y = y + padding;
    int inner_w = width - padding * 2;
    int inner_h = height - padding * 2;
    
    /* Merge styles for children */
    PithStyle merged = view->style;
    if (inherited_style) {
        if (!merged.has_color && inherited_style->has_color) {
            merged.has_color = true;
            merged.color = inherited_style->color;
        }
        if (!merged.has_background && inherited_style->has_background) {
            merged.has_background = true;
            merged.background = inherited_style->background;
        }
        if (!merged.has_bold && inherited_style->has_bold) {
            merged.has_bold = true;
            merged.bold = inherited_style->bold;
        }
    }
    
    switch (view->type) {
        case VIEW_TEXT:
            if (view->as.text.content) {
                render_text(ui, view->as.text.content, inner_x, inner_y, fg, bold);
            }
            break;
            
        case VIEW_TEXTFIELD:
            /* Draw text field with cursor indicator */
            render_rect(ui, inner_x, inner_y, inner_w, 1, ui->config.color_bg);
            render_border(ui, inner_x, inner_y, inner_w, 1, "all", 
                          ui->config.color_border);
            if (view->as.textfield.content) {
                render_text(ui, view->as.textfield.content, 
                           inner_x + 1, inner_y, fg, false);
            }
            break;
            
        case VIEW_BUTTON: {
            /* Draw button with brackets */
            char buf[256];
            snprintf(buf, sizeof(buf), "[ %s ]", 
                     view->as.button.label ? view->as.button.label : "");
            render_text(ui, buf, inner_x, inner_y, fg, bold);
            break;
        }
            
        case VIEW_TEXTURE:
            /* TODO: Load and render texture */
            render_text(ui, "[img]", inner_x, inner_y, fg, false);
            break;
            
        case VIEW_VSTACK: {
            int current_y = inner_y;
            int gap = get_gap(view, inherited_style);

            /* Count fill/spacer children and measure fixed children */
            int fill_count = 0;
            int fixed_height = 0;
            for (size_t i = 0; i < view->as.stack.count; i++) {
                PithView *child = view->as.stack.children[i];
                if (child->style.fill || child->type == VIEW_SPACER) {
                    fill_count++;
                } else {
                    int cw, ch;
                    measure_view(ui, child, &cw, &ch);
                    fixed_height += ch;
                }
            }
            fixed_height += gap * (view->as.stack.count > 0 ? view->as.stack.count - 1 : 0);

            int fill_height = fill_count > 0 ?
                (inner_h - fixed_height) / fill_count : 0;
            if (fill_height < 0) fill_height = 0;

            for (size_t i = 0; i < view->as.stack.count; i++) {
                PithView *child = view->as.stack.children[i];
                int cw, ch;
                measure_view(ui, child, &cw, &ch);

                /* Use full width for vstack children */
                int child_w = inner_w;
                int child_h = (child->style.fill || child->type == VIEW_SPACER) ? fill_height : ch;

                render_view_internal(ui, child, inner_x, current_y,
                                     child_w, child_h, &merged);

                current_y += child_h + gap;
            }
            break;
        }
            
        case VIEW_HSTACK: {
            int current_x = inner_x;
            int gap = get_gap(view, inherited_style);

            /* Count fill/spacer children */
            int fill_count = 0;
            int fixed_width = 0;
            for (size_t i = 0; i < view->as.stack.count; i++) {
                PithView *child = view->as.stack.children[i];
                if (child->style.fill || child->type == VIEW_SPACER) {
                    fill_count++;
                } else {
                    int cw, ch;
                    measure_view(ui, child, &cw, &ch);
                    fixed_width += cw;
                }
            }
            fixed_width += gap * (view->as.stack.count > 0 ? view->as.stack.count - 1 : 0);

            int fill_width = fill_count > 0 ?
                (inner_w - fixed_width) / fill_count : 0;
            if (fill_width < 0) fill_width = 0;

            for (size_t i = 0; i < view->as.stack.count; i++) {
                PithView *child = view->as.stack.children[i];
                int cw, ch;
                measure_view(ui, child, &cw, &ch);

                int child_w = (child->style.fill || child->type == VIEW_SPACER) ? fill_width : cw;

                render_view_internal(ui, child, current_x, inner_y,
                                     child_w, inner_h, &merged);

                current_x += child_w + gap;
            }
            break;
        }

        case VIEW_SPACER:
            /* Spacer is invisible - just takes up space */
            break;
    }
}

void pith_ui_render(PithUI *ui, PithView *view) {
    pith_ui_render_at(ui, view, 0, 0, ui->cells_wide, ui->cells_high);
}

void pith_ui_render_at(PithUI *ui, PithView *view, int x, int y, int width, int height) {
    render_view_internal(ui, view, x, y, width, height, NULL);
}

/* ========================================================================
   INPUT HANDLING
   ======================================================================== */

PithEvent pith_ui_poll_event(PithUI *ui) {
    PithEvent event = { .type = EVENT_NONE };
    
    /* Check for key press */
    int key = GetKeyPressed();
    if (key != 0) {
        event.type = EVENT_KEY;
        event.as.key.key_code = key;
        event.as.key.ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        event.as.key.alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        event.as.key.shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        event.as.key.cmd = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
        return event;
    }
    
    /* Check for text input */
    int ch = GetCharPressed();
    if (ch != 0) {
        event.type = EVENT_TEXT_INPUT;
        /* Convert unicode codepoint to UTF-8 */
        static char text_buf[8];
        if (ch < 0x80) {
            text_buf[0] = ch;
            text_buf[1] = '\0';
        } else {
            /* Simplified UTF-8 encoding */
            text_buf[0] = ch;
            text_buf[1] = '\0';
        }
        event.as.text_input.text = text_buf;
        return event;
    }
    
    /* Check for mouse click */
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        event.type = EVENT_CLICK;
        Vector2 pos = GetMousePosition();
        event.as.click.x = (int)(pos.x / ui->config.cell_width);
        event.as.click.y = (int)(pos.y / ui->config.cell_height);
        event.as.click.button = 0;
        event.as.click.target = NULL; /* TODO: hit testing */
        return event;
    }
    
    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
        event.type = EVENT_CLICK;
        Vector2 pos = GetMousePosition();
        event.as.click.x = (int)(pos.x / ui->config.cell_width);
        event.as.click.y = (int)(pos.y / ui->config.cell_height);
        event.as.click.button = 1;
        event.as.click.target = NULL;
        return event;
    }
    
    return event;
}

/* ========================================================================
   UTILITIES
   ======================================================================== */

void pith_ui_get_size(PithUI *ui, int *width, int *height) {
    *width = ui->cells_wide;
    *height = ui->cells_high;
}

void pith_ui_pixel_to_cell(PithUI *ui, int px, int py, int *cx, int *cy) {
    *cx = px / ui->config.cell_width;
    *cy = py / ui->config.cell_height;
}

void pith_ui_set_title(PithUI *ui, const char *title) {
    SetWindowTitle(title);
}
