/*
 * pith_ui.h - Platform-specific UI rendering
 * 
 * This file defines the interface for rendering Pith views to screen
 * and capturing user input. The current implementation uses raylib,
 * but this interface is designed to be implementable with other
 * graphics libraries.
 */

#ifndef PITH_UI_H
#define PITH_UI_H

#include "pith_types.h"
#include "pith_runtime.h"

/* ========================================================================
   CONFIGURATION
   ======================================================================== */

typedef struct {
    int window_width;       /* Initial window width in pixels */
    int window_height;      /* Initial window height in pixels */
    const char *title;      /* Window title */

    int cell_width;         /* Width of a cell in pixels */
    int cell_height;        /* Height of a cell in pixels */

    const char *font_path;  /* Path to monospace font (or NULL for default) */
    int font_size;          /* Font size in pixels */

    /* Default colors (RGBA) */
    uint32_t color_fg;
    uint32_t color_bg;
    uint32_t color_border;
    uint32_t color_selection;

    bool verbose;           /* Enable verbose raylib logging */
} PithUIConfig;

/* Default configuration */
PithUIConfig pith_ui_default_config(void);

/* ========================================================================
   UI STATE
   ======================================================================== */

typedef struct PithUI PithUI;

/* ========================================================================
   UI LIFECYCLE
   ======================================================================== */

/* Initialize the UI with the given configuration */
PithUI* pith_ui_new(PithUIConfig config);

/* Free UI resources */
void pith_ui_free(PithUI *ui);

/* Check if window should close */
bool pith_ui_should_close(PithUI *ui);

/* ========================================================================
   MAIN LOOP
   ======================================================================== */

/* Begin a frame - call at start of each iteration */
void pith_ui_begin_frame(PithUI *ui);

/* End a frame - call at end of each iteration */
void pith_ui_end_frame(PithUI *ui);

/* ========================================================================
   RENDERING
   ======================================================================== */

/* Render a view tree to the screen */
void pith_ui_render(PithUI *ui, PithView *view);

/* Render at a specific cell position and size */
void pith_ui_render_at(PithUI *ui, PithView *view, int x, int y, int width, int height);

/* ========================================================================
   INPUT
   ======================================================================== */

/* Poll for the next event. Returns EVENT_NONE if no events pending. */
PithEvent pith_ui_poll_event(PithUI *ui);

/* ========================================================================
   UTILITIES
   ======================================================================== */

/* Get window size in cells */
void pith_ui_get_size(PithUI *ui, int *width, int *height);

/* Convert pixel coordinates to cell coordinates */
void pith_ui_pixel_to_cell(PithUI *ui, int px, int py, int *cx, int *cy);

/* Set window title */
void pith_ui_set_title(PithUI *ui, const char *title);

/* ========================================================================
   COLOR HELPERS
   ======================================================================== */

/* Parse a color string ("red", "#ff0000", etc.) to RGBA */
uint32_t pith_color_parse(const char *str);

/* Named colors */
#define PITH_COLOR_BLACK    0x000000FF
#define PITH_COLOR_WHITE    0xFFFFFFFF
#define PITH_COLOR_RED      0xFF0000FF
#define PITH_COLOR_GREEN    0x00FF00FF
#define PITH_COLOR_BLUE     0x0000FFFF
#define PITH_COLOR_YELLOW   0xFFFF00FF
#define PITH_COLOR_CYAN     0x00FFFFFF
#define PITH_COLOR_MAGENTA  0xFF00FFFF
#define PITH_COLOR_GRAY     0x808080FF
#define PITH_COLOR_DARKGRAY 0x404040FF

#endif /* PITH_UI_H */
