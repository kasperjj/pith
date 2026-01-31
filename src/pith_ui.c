/*
 * pith_ui.c - Raylib-based UI renderer for Pith
 * 
 * This is the platform-specific rendering layer. It renders View trees
 * produced by the runtime and captures user input as events.
 */

#include "pith_ui.h"
#include "raylib.h"
#include "font_data.h"
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

    /* High-DPI scale factor */
    float scale;

    /* Scaled cell dimensions */
    int cell_width;
    int cell_height;
    int font_size;

    /* Window size in cells */
    int cells_wide;
    int cells_high;

    /* Input state */
    int last_key;
    bool key_pending;

    /* Focus state */
    PithView *focused_view;

    /* Click state - prevent duplicate click events per frame */
    bool left_click_handled;
    bool right_click_handled;
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

        .font_path = NULL,  /* Uses embedded font by default */
        .font_size = 18,

        .color_fg = PITH_COLOR_WHITE,
        .color_bg = PITH_COLOR_BLACK,
        .color_border = PITH_COLOR_GRAY,
        .color_selection = PITH_COLOR_BLUE,

        .verbose = false,
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

    /* Set raylib log level - suppress unless verbose */
    if (!config.verbose) {
        SetTraceLogLevel(LOG_WARNING);
    }

    /* Initialize raylib */
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    InitWindow(config.window_width, config.window_height, config.title);
    SetTargetFPS(60);

    /* Get DPI scale factor for high-DPI displays */
    Vector2 scale_dpi = GetWindowScaleDPI();
    ui->scale = scale_dpi.x;  /* Assume uniform scaling */

    /* Use logical cell dimensions (raylib handles DPI scaling internally) */
    ui->cell_width = config.cell_width;
    ui->cell_height = config.cell_height;
    /* Load font at scaled size for sharpness on high-DPI displays */
    ui->font_size = (int)(config.font_size * ui->scale);

    /* Load font at scaled size for crisp rendering */
    if (config.font_path && FileExists(config.font_path)) {
        ui->font = LoadFontEx(config.font_path, ui->font_size, NULL, 0);
        if (ui->font.baseSize > 0) {
            ui->font_loaded = true;
        } else {
            ui->font = GetFontDefault();
            ui->font_loaded = false;
        }
    } else {
        /* Use embedded DepartureMono font */
        ui->font = LoadFontFromMemory(".otf", FONT_DATA, FONT_DATA_SIZE,
                                       ui->font_size, NULL, 0);
        if (ui->font.baseSize > 0) {
            ui->font_loaded = true;
        } else {
            ui->font = GetFontDefault();
            ui->font_loaded = false;
        }
    }

    /* Use point filtering for crisp pixel rendering */
    SetTextureFilter(ui->font.texture, TEXTURE_FILTER_POINT);

    /* Calculate initial cell count */
    ui->cells_wide = GetScreenWidth() / ui->cell_width;
    ui->cells_high = GetScreenHeight() / ui->cell_height;

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
    ui->cells_wide = width / ui->cell_width;
    ui->cells_high = height / ui->cell_height;

    /* Reset per-frame state */
    ui->left_click_handled = false;
    ui->right_click_handled = false;

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
    int px = cell_x * ui->cell_width;
    int py = cell_y * ui->cell_height;

    Color c = rgba_to_color(color);

    /* Draw at configured font size - raylib handles high-DPI scaling */
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
    
    int px = x * ui->cell_width;
    int py = y * ui->cell_height;
    int pw = w * ui->cell_width;
    int ph = h * ui->cell_height;
    
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
    int px = x * ui->cell_width;
    int py = y * ui->cell_height;
    int pw = w * ui->cell_width;
    int ph = h * ui->cell_height;
    
    DrawRectangle(px, py, pw, ph, rgba_to_color(color));
}

/* Calculate view size in cells */
static void measure_view(PithUI *ui, PithView *view, int *out_w, int *out_h) {
    if (!view) {
        *out_w = 0;
        *out_h = 0;
        return;
    }

    switch (view->type) {
        case VIEW_TEXT:
            /* Count lines and find max line width */
            if (view->as.text.content) {
                int lines = 1;
                int max_width = 0;
                int current_width = 0;
                for (const char *c = view->as.text.content; *c; c++) {
                    if (*c == '\n') {
                        lines++;
                        if (current_width > max_width) max_width = current_width;
                        current_width = 0;
                    } else {
                        current_width++;
                    }
                }
                if (current_width > max_width) max_width = current_width;
                *out_w = max_width;
                *out_h = lines;
            } else {
                *out_w = 0;
                *out_h = 1;
            }
            break;
            
        case VIEW_TEXTFIELD: {
            /* Measure based on gap buffer content, with minimum width */
            int content_width = 10;
            if (view->as.textfield.buffer) {
                char *str = pith_gapbuf_to_string(view->as.textfield.buffer);
                if (str) {
                    content_width = (int)strlen(str) + 2; /* +2 for padding */
                    free(str);
                }
            }
            *out_w = content_width > 10 ? content_width : 10;
            *out_h = 1;
            break;
        }

        case VIEW_TEXTAREA: {
            /* If fill is set, return 0 so it expands to fill available space */
            if (view->style.fill) {
                *out_w = 0;
                *out_h = 0;
                break;
            }

            /* Measure based on gap buffer content */
            int max_width = 20;  /* Minimum width */
            int line_count = 3;  /* Minimum height */
            if (view->as.textarea.buffer) {
                size_t total_lines = pith_gapbuf_line_count(view->as.textarea.buffer);
                line_count = total_lines > 3 ? (int)total_lines : 3;

                /* Find max line width */
                for (size_t i = 0; i < total_lines; i++) {
                    size_t line_len = pith_gapbuf_line_length(view->as.textarea.buffer, i);
                    if ((int)line_len + 2 > max_width) {
                        max_width = (int)line_len + 2;  /* +2 for padding */
                    }
                }
            }
            *out_w = max_width;
            /* Use style.height if set, otherwise content height */
            if (view->style.has_height && view->style.height > 0) {
                *out_h = view->style.height;
            } else {
                *out_h = line_count;
            }
            break;
        }

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

        default:
            /* Unknown view type */
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

/* Hit test - find view at cell coordinates */
static PithView* hit_test_internal(PithUI *ui, PithView *view,
                                    int x, int y, int width, int height,
                                    int test_x, int test_y) {
    if (!view) return NULL;

    /* Check if point is within this view's bounds */
    if (test_x < x || test_x >= x + width ||
        test_y < y || test_y >= y + height) {
        return NULL;
    }

    int padding = get_padding(view, NULL);
    int inner_x = x + padding;
    int inner_y = y + padding;
    int inner_w = width - padding * 2;
    int inner_h = height - padding * 2;

    /* For container views, check children first (front to back) */
    switch (view->type) {
        case VIEW_VSTACK: {
            int current_y = inner_y;
            int gap = get_gap(view, NULL);
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
            int fill_height = fill_count > 0 ? (inner_h - fixed_height) / fill_count : 0;
            if (fill_height < 0) fill_height = 0;

            for (size_t i = 0; i < view->as.stack.count; i++) {
                PithView *child = view->as.stack.children[i];
                if (!child) continue;
                int cw, ch;
                measure_view(ui, child, &cw, &ch);
                int child_h = (child->style.fill || child->type == VIEW_SPACER) ? fill_height : ch;
                PithView *hit = hit_test_internal(ui, child, inner_x, current_y,
                                                   inner_w, child_h, test_x, test_y);
                if (hit) return hit;
                current_y += child_h + gap;
            }
            break;
        }
        case VIEW_HSTACK: {
            int current_x = inner_x;
            int gap = get_gap(view, NULL);
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
            int fill_width = fill_count > 0 ? (inner_w - fixed_width) / fill_count : 0;
            if (fill_width < 0) fill_width = 0;

            for (size_t i = 0; i < view->as.stack.count; i++) {
                PithView *child = view->as.stack.children[i];
                if (!child) continue;
                int cw, ch;
                measure_view(ui, child, &cw, &ch);
                int child_w = (child->style.fill || child->type == VIEW_SPACER) ? fill_width : cw;
                PithView *hit = hit_test_internal(ui, child, current_x, inner_y,
                                                   child_w, inner_h, test_x, test_y);
                if (hit) return hit;
                current_x += child_w + gap;
            }
            break;
        }
        default:
            break;
    }

    /* Return this view if it's a focusable type */
    if (view->type == VIEW_TEXTFIELD || view->type == VIEW_TEXTAREA ||
        view->type == VIEW_BUTTON) {
        return view;
    }

    return NULL;
}

/* Public hit test function */
PithView* pith_ui_hit_test(PithUI *ui, PithView *root, int cell_x, int cell_y) {
    return hit_test_internal(ui, root, 0, 0, ui->cells_wide, ui->cells_high,
                              cell_x, cell_y);
}

/* Internal rendering function */
static void render_view_internal(PithUI *ui, PithView *view,
                                  int x, int y, int width, int height,
                                  PithStyle *inherited_style) {
    if (!view) return;

    /* Cache render position for click handling */
    view->render_x = x;
    view->render_y = y;
    view->render_w = width;
    view->render_h = height;

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
            
        case VIEW_TEXTFIELD: {
            /* Draw text field with light background for contrast */
            uint32_t field_bg = view->style.has_background ? bg : 0xf1f3f5ff; /* gray 1 */
            uint32_t field_fg = view->style.has_color ? fg : 0x212529ff; /* gray 9 */

            render_rect(ui, inner_x, inner_y, inner_w, 1, field_bg);
            render_border(ui, inner_x, inner_y, inner_w, 1, "all",
                          ui->config.color_border);

            /* Get content from gap buffer */
            if (view->as.textfield.buffer) {
                char *content = pith_gapbuf_to_string(view->as.textfield.buffer);
                if (content) {
                    render_text(ui, content, inner_x + 1, inner_y, field_fg, false);

                    /* Draw cursor if this field is focused */
                    if (ui->focused_view == view) {
                        size_t cursor_pos = pith_gapbuf_cursor(view->as.textfield.buffer);
                        int cursor_x = inner_x + 1 + (int)cursor_pos;
                        /* Draw cursor as a vertical bar */
                        int px = cursor_x * ui->cell_width;
                        int py = inner_y * ui->cell_height;
                        DrawRectangle(px, py, 2, ui->cell_height,
                                     rgba_to_color(field_fg));
                    }
                    free(content);
                }
            }
            break;
        }

        case VIEW_TEXTAREA: {
            /* Draw textarea with light background */
            uint32_t field_bg = view->style.has_background ? bg : 0xf1f3f5ff; /* gray 1 */
            uint32_t field_fg = view->style.has_color ? fg : 0x212529ff; /* gray 9 */

            render_rect(ui, inner_x, inner_y, inner_w, inner_h, field_bg);
            render_border(ui, inner_x, inner_y, inner_w, inner_h, "all",
                          ui->config.color_border);

            /* Render text line by line */
            if (view->as.textarea.buffer) {
                PithGapBuffer *buf = view->as.textarea.buffer;
                size_t total_lines = pith_gapbuf_line_count(buf);
                int scroll_offset = view->as.textarea.scroll_offset;
                int visible_lines = inner_h;

                /* Cache visible height for scroll calculations */
                view->as.textarea.visible_height = visible_lines;

                /* Render each visible line */
                for (int line_idx = 0; line_idx < visible_lines; line_idx++) {
                    size_t line_num = scroll_offset + line_idx;
                    if (line_num >= total_lines) break;

                    /* Extract line content */
                    size_t line_start = pith_gapbuf_line_start(buf, line_num);
                    size_t line_len = pith_gapbuf_line_length(buf, line_num);

                    /* Build line string - fit within available width */
                    int max_chars = inner_w - 2;  /* Leave space for padding */
                    if (max_chars < 0) max_chars = 0;
                    size_t chars_to_copy = line_len < (size_t)max_chars ? line_len : (size_t)max_chars;

                    char *line_buf = malloc(chars_to_copy + 1);
                    for (size_t i = 0; i < chars_to_copy; i++) {
                        line_buf[i] = pith_gapbuf_char_at(buf, line_start + i);
                    }
                    line_buf[chars_to_copy] = '\0';

                    render_text(ui, line_buf, inner_x + 1, inner_y + line_idx, field_fg, false);
                    free(line_buf);
                }

                /* Draw cursor if focused */
                if (ui->focused_view == view) {
                    size_t cursor_line = pith_gapbuf_cursor_line(buf);
                    size_t cursor_col = pith_gapbuf_cursor_column(buf);

                    /* Check if cursor is in visible area */
                    if ((int)cursor_line >= scroll_offset &&
                        (int)cursor_line < scroll_offset + visible_lines) {
                        int cursor_screen_y = inner_y + (int)cursor_line - scroll_offset;
                        int cursor_screen_x = inner_x + 1 + (int)cursor_col;

                        /* Draw cursor as a vertical bar */
                        int px = cursor_screen_x * ui->cell_width;
                        int py = cursor_screen_y * ui->cell_height;
                        DrawRectangle(px, py, 2, ui->cell_height,
                                     rgba_to_color(field_fg));
                    }
                }
            }
            break;
        }

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
    
    /* Check for mouse click (only once per frame) */
    if (!ui->left_click_handled && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        ui->left_click_handled = true;
        event.type = EVENT_CLICK;
        Vector2 pos = GetMousePosition();
        event.as.click.x = (int)(pos.x / ui->cell_width);
        event.as.click.y = (int)(pos.y / ui->cell_height);
        event.as.click.button = 0;
        event.as.click.target = NULL;
        return event;
    }

    if (!ui->right_click_handled && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
        ui->right_click_handled = true;
        event.type = EVENT_CLICK;
        Vector2 pos = GetMousePosition();
        event.as.click.x = (int)(pos.x / ui->cell_width);
        event.as.click.y = (int)(pos.y / ui->cell_height);
        event.as.click.button = 1;
        event.as.click.target = NULL;
        return event;
    }
    
    return event;
}

/* ========================================================================
   FOCUS MANAGEMENT
   ======================================================================== */

void pith_ui_set_focus(PithUI *ui, PithView *view) {
    ui->focused_view = view;
}

PithView* pith_ui_get_focus(PithUI *ui) {
    return ui->focused_view;
}

/* ========================================================================
   TEXTFIELD / TEXTAREA INPUT HANDLING
   ======================================================================== */

/* Update scroll offset to keep cursor visible */
static void update_textarea_scroll(PithView *view) {
    if (view->type != VIEW_TEXTAREA || !view->as.textarea.buffer) return;

    PithGapBuffer *buf = view->as.textarea.buffer;
    size_t cursor_line = pith_gapbuf_cursor_line(buf);
    int scroll_offset = view->as.textarea.scroll_offset;

    /* Get visible height: use cached value from render, or style, or default */
    int visible_lines = 3;
    if (view->as.textarea.visible_height > 0) {
        visible_lines = view->as.textarea.visible_height;
    } else if (view->style.has_height && view->style.height > 0) {
        visible_lines = view->style.height;
    }

    /* Adjust scroll to keep cursor visible */
    if ((int)cursor_line < scroll_offset) {
        /* Cursor is above visible area */
        view->as.textarea.scroll_offset = (int)cursor_line;
    } else if ((int)cursor_line >= scroll_offset + visible_lines) {
        /* Cursor is below visible area */
        view->as.textarea.scroll_offset = (int)cursor_line - visible_lines + 1;
    }
}

bool pith_ui_handle_textfield_input(PithUI *ui, PithEvent event) {
    if (!ui->focused_view) return false;

    PithViewType type = ui->focused_view->type;
    bool is_textfield = (type == VIEW_TEXTFIELD);
    bool is_textarea = (type == VIEW_TEXTAREA);

    if (!is_textfield && !is_textarea) {
        return false;
    }

    /* Get the gap buffer from whichever type it is */
    PithGapBuffer *buf = is_textfield
        ? ui->focused_view->as.textfield.buffer
        : ui->focused_view->as.textarea.buffer;
    if (!buf) return false;

    if (event.type == EVENT_TEXT_INPUT) {
        /* Insert typed character */
        pith_gapbuf_insert(buf, event.as.text_input.text);
        if (is_textarea) update_textarea_scroll(ui->focused_view);
        return true;
    }

    if (event.type == EVENT_KEY) {
        int key = event.as.key.key_code;

        /* Backspace - delete character before cursor */
        if (key == KEY_BACKSPACE) {
            pith_gapbuf_delete(buf, -1);
            if (is_textarea) update_textarea_scroll(ui->focused_view);
            return true;
        }

        /* Delete - delete character after cursor */
        if (key == KEY_DELETE) {
            pith_gapbuf_delete(buf, 1);
            return true;
        }

        /* Left arrow - move cursor left */
        if (key == KEY_LEFT) {
            pith_gapbuf_move(buf, -1);
            if (is_textarea) update_textarea_scroll(ui->focused_view);
            return true;
        }

        /* Right arrow - move cursor right */
        if (key == KEY_RIGHT) {
            pith_gapbuf_move(buf, 1);
            if (is_textarea) update_textarea_scroll(ui->focused_view);
            return true;
        }

        /* Up arrow - move cursor up (textarea only) */
        if (key == KEY_UP && is_textarea) {
            pith_gapbuf_move_up(buf, 1);
            update_textarea_scroll(ui->focused_view);
            return true;
        }

        /* Down arrow - move cursor down (textarea only) */
        if (key == KEY_DOWN && is_textarea) {
            pith_gapbuf_move_down(buf, 1);
            update_textarea_scroll(ui->focused_view);
            return true;
        }

        /* Home - move to line start (textarea) or buffer start (textfield) */
        if (key == KEY_HOME) {
            if (is_textarea) {
                pith_gapbuf_line_home(buf);
                update_textarea_scroll(ui->focused_view);
            } else {
                pith_gapbuf_goto(buf, 0);
            }
            return true;
        }

        /* End - move to line end (textarea) or buffer end (textfield) */
        if (key == KEY_END) {
            if (is_textarea) {
                pith_gapbuf_line_end_move(buf);
            } else {
                size_t len = pith_gapbuf_length(buf);
                pith_gapbuf_goto(buf, len);
            }
            return true;
        }

        /* Enter - insert newline (textarea only) */
        if (key == KEY_ENTER && is_textarea) {
            pith_gapbuf_insert(buf, "\n");
            update_textarea_scroll(ui->focused_view);
            return true;
        }

        /* Escape - unfocus */
        if (key == KEY_ESCAPE) {
            ui->focused_view = NULL;
            return true;
        }
    }

    return false;
}

/* Position cursor in textfield/textarea based on click coordinates */
void pith_ui_click_to_cursor(PithView *view, int click_x, int click_y) {
    if (!view) return;

    if (view->type == VIEW_TEXTFIELD) {
        PithGapBuffer *buf = view->as.textfield.buffer;
        if (!buf) return;

        /* Calculate position within the textfield */
        /* render_x + 1 is where text starts (1 cell padding) */
        int text_start_x = view->render_x + 1;
        int char_pos = click_x - text_start_x;

        if (char_pos < 0) char_pos = 0;

        /* Clamp to buffer length */
        size_t len = pith_gapbuf_length(buf);
        if ((size_t)char_pos > len) char_pos = (int)len;

        pith_gapbuf_goto(buf, (size_t)char_pos);

    } else if (view->type == VIEW_TEXTAREA) {
        PithGapBuffer *buf = view->as.textarea.buffer;
        if (!buf) return;

        /* Calculate position within the textarea */
        /* render_x + 1 is where text starts (1 cell padding) */
        /* render_y is where the first visible line starts */
        int text_start_x = view->render_x + 1;
        int text_start_y = view->render_y;

        int col = click_x - text_start_x;
        int visible_line = click_y - text_start_y;

        if (col < 0) col = 0;
        if (visible_line < 0) visible_line = 0;

        /* Convert visible line to actual line number using scroll offset */
        int scroll_offset = view->as.textarea.scroll_offset;
        size_t line = (size_t)(scroll_offset + visible_line);

        /* Clamp to valid line range */
        size_t total_lines = pith_gapbuf_line_count(buf);
        if (line >= total_lines) {
            line = total_lines > 0 ? total_lines - 1 : 0;
        }

        /* Clamp column to line length */
        size_t line_len = pith_gapbuf_line_length(buf, line);
        if ((size_t)col > line_len) col = (int)line_len;

        /* Move cursor to the calculated position */
        size_t pos = pith_gapbuf_pos_from_line_col(buf, line, (size_t)col);
        pith_gapbuf_goto(buf, pos);
    }
}

/* Commit text widget content to its source signal */
void pith_ui_commit_text_widget(PithView *view) {
    if (!view) return;

    if (view->type == VIEW_TEXTFIELD) {
        PithSignal *sig = view->as.textfield.source_signal;
        if (sig && view->as.textfield.buffer) {
            char *content = pith_gapbuf_to_string(view->as.textfield.buffer);
            pith_signal_set(sig, PITH_STRING(content));
        }
    } else if (view->type == VIEW_TEXTAREA) {
        PithSignal *sig = view->as.textarea.source_signal;
        if (sig && view->as.textarea.buffer) {
            char *content = pith_gapbuf_to_string(view->as.textarea.buffer);
            pith_signal_set(sig, PITH_STRING(content));
        }
    }
}

/* ========================================================================
   UTILITIES
   ======================================================================== */

void pith_ui_get_size(PithUI *ui, int *width, int *height) {
    *width = ui->cells_wide;
    *height = ui->cells_high;
}

void pith_ui_pixel_to_cell(PithUI *ui, int px, int py, int *cx, int *cy) {
    *cx = px / ui->cell_width;
    *cy = py / ui->cell_height;
}

void pith_ui_set_title(PithUI *ui, const char *title) {
    SetWindowTitle(title);
}
