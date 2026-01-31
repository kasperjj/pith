/*
 * pith_runtime.c - Platform-independent Pith interpreter
 */

#include "pith_runtime.h"
#include "pith_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>

/* Forward declarations */
static PithDict* pith_find_dict(PithRuntime *rt, const char *name);

/* ========================================================================
   MEMORY HELPERS
   ======================================================================== */

static char* pith_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

/* ========================================================================
   ERROR HANDLING
   ======================================================================== */

void pith_error(PithRuntime *rt, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(rt->error, PITH_ERROR_MAX, fmt, args);
    va_end(args);
    rt->has_error = true;
}

void pith_clear_error(PithRuntime *rt) {
    rt->has_error = false;
    rt->error[0] = '\0';
}

const char* pith_get_error(PithRuntime *rt) {
    return rt->has_error ? rt->error : NULL;
}

/* ========================================================================
   STACK OPERATIONS
   ======================================================================== */

/* Global debug flag - declared in main.c */
extern bool g_debug;

bool pith_push(PithRuntime *rt, PithValue value) {
    if (rt->stack_top >= PITH_STACK_MAX) {
        pith_error(rt, "Stack overflow");
        if (g_debug) {
            fprintf(stderr, "[DEBUG] Stack overflow! Dumping stack:\n");
            for (size_t i = 0; i < rt->stack_top && i < 20; i++) {
                fprintf(stderr, "  [%zu] type=%d\n", i, rt->stack[i].type);
            }
        }
        return false;
    }
    rt->stack[rt->stack_top++] = value;
    return true;
}

PithValue pith_pop(PithRuntime *rt) {
    if (rt->stack_top == 0) {
        pith_error(rt, "Stack underflow");
        return PITH_NIL();
    }
    return rt->stack[--rt->stack_top];
}

PithValue pith_peek(PithRuntime *rt) {
    if (rt->stack_top == 0) {
        pith_error(rt, "Stack underflow");
        return PITH_NIL();
    }
    return rt->stack[rt->stack_top - 1];
}

bool pith_stack_has(PithRuntime *rt, size_t n) {
    return rt->stack_top >= n;
}

/* ========================================================================
   ARRAY HELPERS
   ======================================================================== */

PithArray* pith_array_new(void) {
    PithArray *array = malloc(sizeof(PithArray));
    array->items = NULL;
    array->length = 0;
    array->capacity = 0;
    return array;
}

void pith_array_free(PithArray *array) {
    if (!array) return;
    for (size_t i = 0; i < array->length; i++) {
        pith_value_free(array->items[i]);
    }
    free(array->items);
    free(array);
}

void pith_array_push(PithArray *array, PithValue value) {
    if (array->length >= array->capacity) {
        array->capacity = array->capacity ? array->capacity * 2 : 8;
        array->items = realloc(array->items, array->capacity * sizeof(PithValue));
    }
    array->items[array->length++] = value;
}

PithValue pith_array_pop(PithArray *array) {
    if (array->length == 0) return PITH_NIL();
    return array->items[--array->length];
}

PithValue pith_array_get(PithArray *array, size_t index) {
    if (index >= array->length) return PITH_NIL();
    return array->items[index];
}

/* ========================================================================
   GAP BUFFER HELPERS
   ======================================================================== */

#define GAP_BUFFER_INITIAL_SIZE 64
#define GAP_BUFFER_MIN_GAP 32

PithGapBuffer* pith_gapbuf_new(void) {
    PithGapBuffer *gb = malloc(sizeof(PithGapBuffer));
    gb->capacity = GAP_BUFFER_INITIAL_SIZE;
    gb->buffer = malloc(gb->capacity);
    gb->gap_start = 0;
    gb->gap_end = gb->capacity;
    return gb;
}

PithGapBuffer* pith_gapbuf_from_string(const char *str) {
    size_t len = str ? strlen(str) : 0;
    PithGapBuffer *gb = malloc(sizeof(PithGapBuffer));
    gb->capacity = len + GAP_BUFFER_MIN_GAP;
    gb->buffer = malloc(gb->capacity);
    /* Place content after the gap, cursor at position 0 */
    gb->gap_start = 0;
    gb->gap_end = GAP_BUFFER_MIN_GAP;
    if (len > 0) {
        memcpy(gb->buffer + gb->gap_end, str, len);
    }
    return gb;
}

void pith_gapbuf_free(PithGapBuffer *gb) {
    if (!gb) return;
    free(gb->buffer);
    free(gb);
}

PithGapBuffer* pith_gapbuf_copy(PithGapBuffer *gb) {
    if (!gb) return pith_gapbuf_new();
    PithGapBuffer *copy = malloc(sizeof(PithGapBuffer));
    copy->capacity = gb->capacity;
    copy->buffer = malloc(copy->capacity);
    memcpy(copy->buffer, gb->buffer, gb->capacity);
    copy->gap_start = gb->gap_start;
    copy->gap_end = gb->gap_end;
    return copy;
}

/* Get the content length (excluding the gap) */
static size_t pith_gapbuf_length(PithGapBuffer *gb) {
    return gb->capacity - (gb->gap_end - gb->gap_start);
}

/* Get the gap size */
static size_t pith_gapbuf_gap_size(PithGapBuffer *gb) {
    return gb->gap_end - gb->gap_start;
}

/* Ensure the gap is at least min_size bytes */
static void pith_gapbuf_expand_gap(PithGapBuffer *gb, size_t min_size) {
    size_t gap_size = pith_gapbuf_gap_size(gb);
    if (gap_size >= min_size) return;

    size_t need = min_size - gap_size;
    size_t new_capacity = gb->capacity + need + GAP_BUFFER_MIN_GAP;
    char *new_buffer = malloc(new_capacity);

    /* Copy pre-gap content */
    memcpy(new_buffer, gb->buffer, gb->gap_start);
    /* Copy post-gap content to new position */
    size_t post_gap_len = gb->capacity - gb->gap_end;
    memcpy(new_buffer + new_capacity - post_gap_len,
           gb->buffer + gb->gap_end, post_gap_len);

    free(gb->buffer);
    gb->buffer = new_buffer;
    gb->gap_end = new_capacity - post_gap_len;
    gb->capacity = new_capacity;
}

/* Move the gap to position pos in the content */
static void pith_gapbuf_move_gap(PithGapBuffer *gb, size_t pos) {
    size_t len = pith_gapbuf_length(gb);
    if (pos > len) pos = len;
    if (pos == gb->gap_start) return;

    size_t gap_size = pith_gapbuf_gap_size(gb);

    if (pos < gb->gap_start) {
        /* Move gap left: shift content right into gap */
        size_t shift = gb->gap_start - pos;
        memmove(gb->buffer + gb->gap_end - shift,
                gb->buffer + pos, shift);
        gb->gap_start = pos;
        gb->gap_end -= shift;
    } else {
        /* Move gap right: shift content left into gap */
        size_t shift = pos - gb->gap_start;
        memmove(gb->buffer + gb->gap_start,
                gb->buffer + gb->gap_end, shift);
        gb->gap_start += shift;
        gb->gap_end += shift;
    }
}

/* Insert string at current gap position */
void pith_gapbuf_insert(PithGapBuffer *gb, const char *str) {
    if (!str) return;
    size_t len = strlen(str);
    if (len == 0) return;

    pith_gapbuf_expand_gap(gb, len);
    memcpy(gb->buffer + gb->gap_start, str, len);
    gb->gap_start += len;
}

/* Delete n characters: positive = forward (after cursor), negative = backward */
void pith_gapbuf_delete(PithGapBuffer *gb, int n) {
    if (n > 0) {
        /* Delete forward: expand gap to the right */
        size_t post_gap = gb->capacity - gb->gap_end;
        if ((size_t)n > post_gap) n = post_gap;
        gb->gap_end += n;
    } else if (n < 0) {
        /* Delete backward: expand gap to the left */
        size_t avail = gb->gap_start;
        if ((size_t)(-n) > avail) n = -(int)avail;
        gb->gap_start += n;  /* n is negative, so this decreases gap_start */
    }
}

/* Move cursor by delta positions */
void pith_gapbuf_move(PithGapBuffer *gb, int delta) {
    size_t len = pith_gapbuf_length(gb);
    int new_pos = (int)gb->gap_start + delta;
    if (new_pos < 0) new_pos = 0;
    if ((size_t)new_pos > len) new_pos = len;
    pith_gapbuf_move_gap(gb, new_pos);
}

/* Move cursor to absolute position */
void pith_gapbuf_goto(PithGapBuffer *gb, size_t pos) {
    pith_gapbuf_move_gap(gb, pos);
}

/* Get cursor position */
size_t pith_gapbuf_cursor(PithGapBuffer *gb) {
    return gb->gap_start;
}

/* Get character at position (returns '\0' if out of bounds) */
char pith_gapbuf_char_at(PithGapBuffer *gb, size_t pos) {
    size_t len = pith_gapbuf_length(gb);
    if (pos >= len) return '\0';

    if (pos < gb->gap_start) {
        return gb->buffer[pos];
    } else {
        return gb->buffer[gb->gap_end + (pos - gb->gap_start)];
    }
}

/* Convert to string */
char* pith_gapbuf_to_string(PithGapBuffer *gb) {
    size_t len = pith_gapbuf_length(gb);
    char *str = malloc(len + 1);

    /* Copy pre-gap content */
    memcpy(str, gb->buffer, gb->gap_start);
    /* Copy post-gap content */
    memcpy(str + gb->gap_start, gb->buffer + gb->gap_end,
           gb->capacity - gb->gap_end);
    str[len] = '\0';

    return str;
}

/* ========================================================================
   SIGNAL HELPERS
   ======================================================================== */

PithSignal* pith_signal_new(PithRuntime *rt, PithValue initial) {
    PithSignal *sig = malloc(sizeof(PithSignal));
    sig->value = initial;
    sig->subscribers = NULL;
    sig->subscriber_count = 0;
    sig->subscriber_capacity = 0;
    sig->dirty = false;

    /* Register signal with runtime for dirty checking */
    if (rt) {
        if (rt->signal_count >= rt->signal_capacity) {
            rt->signal_capacity = rt->signal_capacity ? rt->signal_capacity * 2 : 8;
            rt->all_signals = realloc(rt->all_signals, rt->signal_capacity * sizeof(PithSignal*));
        }
        rt->all_signals[rt->signal_count++] = sig;
    }

    return sig;
}

void pith_signal_free(PithSignal *sig) {
    if (!sig) return;
    pith_value_free(sig->value);
    free(sig->subscribers);
    free(sig);
}

void pith_signal_set(PithSignal *sig, PithValue value) {
    if (!sig) return;
    pith_value_free(sig->value);
    sig->value = value;
    sig->dirty = true;
}

PithValue pith_signal_get(PithSignal *sig) {
    if (!sig) return PITH_NIL();
    return sig->value;
}

bool pith_runtime_has_dirty_signals(PithRuntime *rt) {
    for (size_t i = 0; i < rt->signal_count; i++) {
        if (rt->all_signals[i]->dirty) {
            return true;
        }
    }
    return false;
}

void pith_runtime_clear_dirty(PithRuntime *rt) {
    for (size_t i = 0; i < rt->signal_count; i++) {
        rt->all_signals[i]->dirty = false;
    }
}

/* ========================================================================
   MAP HELPERS
   ======================================================================== */

PithMap* pith_map_new(void) {
    PithMap *map = malloc(sizeof(PithMap));
    map->entries = NULL;
    map->length = 0;
    map->capacity = 0;
    return map;
}

void pith_map_free(PithMap *map) {
    if (!map) return;
    for (size_t i = 0; i < map->length; i++) {
        free(map->entries[i].key);
        pith_value_free(map->entries[i].value);
    }
    free(map->entries);
    free(map);
}

void pith_map_set(PithMap *map, const char *key, PithValue value) {
    /* Check if key exists */
    for (size_t i = 0; i < map->length; i++) {
        if (strcmp(map->entries[i].key, key) == 0) {
            pith_value_free(map->entries[i].value);
            map->entries[i].value = value;
            return;
        }
    }
    
    /* Add new entry */
    if (map->length >= map->capacity) {
        map->capacity = map->capacity ? map->capacity * 2 : 8;
        map->entries = realloc(map->entries, map->capacity * sizeof(PithMapEntry));
    }
    map->entries[map->length].key = pith_strdup(key);
    map->entries[map->length].value = value;
    map->length++;
}

PithValue* pith_map_get(PithMap *map, const char *key) {
    for (size_t i = 0; i < map->length; i++) {
        if (strcmp(map->entries[i].key, key) == 0) {
            return &map->entries[i].value;
        }
    }
    return NULL;
}

bool pith_map_has(PithMap *map, const char *key) {
    return pith_map_get(map, key) != NULL;
}

/* ========================================================================
   VALUE HELPERS
   ======================================================================== */

PithValue pith_value_copy(PithValue value) {
    switch (value.type) {
        case VAL_NIL:
        case VAL_BOOL:
        case VAL_NUMBER:
            return value;
            
        case VAL_STRING:
            return PITH_STRING(pith_strdup(value.as.string));
            
        case VAL_ARRAY: {
            PithArray *copy = pith_array_new();
            for (size_t i = 0; i < value.as.array->length; i++) {
                pith_array_push(copy, pith_value_copy(value.as.array->items[i]));
            }
            return PITH_ARRAY(copy);
        }
        
        case VAL_MAP: {
            PithMap *copy = pith_map_new();
            for (size_t i = 0; i < value.as.map->length; i++) {
                pith_map_set(copy, 
                    value.as.map->entries[i].key,
                    pith_value_copy(value.as.map->entries[i].value));
            }
            return PITH_MAP(copy);
        }
        
        case VAL_BLOCK: {
            PithBlock *copy = malloc(sizeof(PithBlock));
            *copy = *value.as.block;
            return PITH_BLOCK(copy);
        }
        
        case VAL_VIEW:
        case VAL_DICT:
        case VAL_SIGNAL:
            /* These are references, don't deep copy */
            return value;

        case VAL_GAPBUF:
            return PITH_GAPBUF(pith_gapbuf_copy(value.as.gapbuf));
    }
    return PITH_NIL();
}

void pith_value_free(PithValue value) {
    switch (value.type) {
        case VAL_STRING:
            free(value.as.string);
            break;
        case VAL_ARRAY:
            pith_array_free(value.as.array);
            break;
        case VAL_MAP:
            pith_map_free(value.as.map);
            break;
        case VAL_BLOCK:
            free(value.as.block);
            break;
        case VAL_DICT:
            pith_dict_free(value.as.dict);
            break;
        case VAL_GAPBUF:
            pith_gapbuf_free(value.as.gapbuf);
            break;
        default:
            break;
    }
}

char* pith_value_to_string(PithValue value) {
    char buf[256];
    switch (value.type) {
        case VAL_NIL:
            return pith_strdup("nil");
        case VAL_BOOL:
            return pith_strdup(value.as.boolean ? "true" : "false");
        case VAL_NUMBER:
            snprintf(buf, sizeof(buf), "%g", value.as.number);
            return pith_strdup(buf);
        case VAL_STRING:
            return pith_strdup(value.as.string);
        case VAL_ARRAY:
            snprintf(buf, sizeof(buf), "[array:%zu]", value.as.array->length);
            return pith_strdup(buf);
        case VAL_MAP:
            snprintf(buf, sizeof(buf), "{map:%zu}", value.as.map->length);
            return pith_strdup(buf);
        case VAL_BLOCK:
            return pith_strdup("[block]");
        case VAL_VIEW:
            return pith_strdup("[view]");
        case VAL_DICT:
            return pith_strdup(value.as.dict->name ? value.as.dict->name : "[dict]");
        case VAL_GAPBUF:
            return pith_gapbuf_to_string(value.as.gapbuf);
        case VAL_SIGNAL:
            return pith_value_to_string(value.as.signal->value);
    }
    return pith_strdup("?");
}

bool pith_value_equal(PithValue a, PithValue b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_NIL:
            return true;
        case VAL_BOOL:
            return a.as.boolean == b.as.boolean;
        case VAL_NUMBER:
            return a.as.number == b.as.number;
        case VAL_STRING:
            return strcmp(a.as.string, b.as.string) == 0;
        default:
            return false; /* Reference equality for complex types */
    }
}

/* ========================================================================
   DICTIONARY OPERATIONS
   ======================================================================== */

PithDict* pith_dict_new(const char *name) {
    PithDict *dict = malloc(sizeof(PithDict));
    dict->name = name ? pith_strdup(name) : NULL;
    dict->parent = NULL;
    dict->slots = NULL;
    dict->slot_count = 0;
    dict->slot_capacity = 0;
    return dict;
}

void pith_dict_free(PithDict *dict) {
    if (!dict) return;
    free(dict->name);
    for (size_t i = 0; i < dict->slot_count; i++) {
        free(dict->slots[i].name);
        if (dict->slots[i].is_cached) {
            pith_value_free(dict->slots[i].cached);
        }
    }
    free(dict->slots);
    free(dict);
}

void pith_dict_set_parent(PithDict *dict, PithDict *parent) {
    dict->parent = parent;
}

bool pith_dict_add_slot(PithDict *dict, const char *name, size_t body_start, size_t body_end) {
    if (dict->slot_count >= dict->slot_capacity) {
        dict->slot_capacity = dict->slot_capacity ? dict->slot_capacity * 2 : 8;
        dict->slots = realloc(dict->slots, dict->slot_capacity * sizeof(PithSlot));
    }
    
    PithSlot *slot = &dict->slots[dict->slot_count++];
    slot->name = pith_strdup(name);
    slot->body_start = body_start;
    slot->body_end = body_end;
    slot->is_cached = false;
    
    return true;
}

PithSlot* pith_dict_lookup(PithDict *dict, const char *name) {
    /* Search local slots first */
    for (size_t i = 0; i < dict->slot_count; i++) {
        if (strcmp(dict->slots[i].name, name) == 0) {
            return &dict->slots[i];
        }
    }
    
    /* Follow parent chain */
    if (dict->parent) {
        return pith_dict_lookup(dict->parent, name);
    }
    
    return NULL;
}

/* Set a slot's value directly (for map operations) */
static void pith_dict_set_value(PithDict *dict, const char *name, PithValue value) {
    /* Check if slot already exists in this dict (not parent) */
    for (size_t i = 0; i < dict->slot_count; i++) {
        if (strcmp(dict->slots[i].name, name) == 0) {
            /* Update existing slot */
            if (dict->slots[i].is_cached) {
                pith_value_free(dict->slots[i].cached);
            }
            dict->slots[i].is_cached = true;
            dict->slots[i].cached = value;
            dict->slots[i].body_start = 0;
            dict->slots[i].body_end = 0;
            return;
        }
    }

    /* Add new slot */
    if (dict->slot_count >= dict->slot_capacity) {
        dict->slot_capacity = dict->slot_capacity ? dict->slot_capacity * 2 : 8;
        dict->slots = realloc(dict->slots, dict->slot_capacity * sizeof(PithSlot));
    }

    PithSlot *slot = &dict->slots[dict->slot_count++];
    slot->name = pith_strdup(name);
    slot->body_start = 0;
    slot->body_end = 0;
    slot->is_cached = true;
    slot->cached = value;
}

/* Copy a dict (shallow copy of slots) */
static PithDict* pith_dict_copy(PithDict *src) {
    PithDict *copy = pith_dict_new(src->name);
    copy->parent = src->parent;

    for (size_t i = 0; i < src->slot_count; i++) {
        PithSlot *s = &src->slots[i];
        if (s->is_cached) {
            pith_dict_set_value(copy, s->name, pith_value_copy(s->cached));
        } else {
            pith_dict_add_slot(copy, s->name, s->body_start, s->body_end);
        }
    }

    return copy;
}

/* Remove a slot from a dict by name */
static bool pith_dict_remove_slot(PithDict *dict, const char *name) {
    for (size_t i = 0; i < dict->slot_count; i++) {
        if (strcmp(dict->slots[i].name, name) == 0) {
            /* Free the slot's resources */
            free(dict->slots[i].name);
            if (dict->slots[i].is_cached) {
                pith_value_free(dict->slots[i].cached);
            }
            /* Shift remaining slots */
            for (size_t j = i; j < dict->slot_count - 1; j++) {
                dict->slots[j] = dict->slots[j + 1];
            }
            dict->slot_count--;
            return true;
        }
    }
    return false;
}

/* ========================================================================
   VIEW HELPERS
   ======================================================================== */

PithView* pith_view_text(const char *content) {
    PithView *view = malloc(sizeof(PithView));
    memset(view, 0, sizeof(PithView));
    view->type = VIEW_TEXT;
    view->as.text.content = pith_strdup(content);
    return view;
}

PithView* pith_view_textfield(const char *content, PithBlock *on_change) {
    PithView *view = malloc(sizeof(PithView));
    memset(view, 0, sizeof(PithView));
    view->type = VIEW_TEXTFIELD;
    view->as.textfield.buffer = pith_gapbuf_from_string(content ? content : "");
    view->as.textfield.on_change = on_change;
    return view;
}

PithView* pith_view_button(const char *label, PithBlock *on_click) {
    PithView *view = malloc(sizeof(PithView));
    memset(view, 0, sizeof(PithView));
    view->type = VIEW_BUTTON;
    view->as.button.label = pith_strdup(label);
    view->as.button.on_click = on_click;
    return view;
}

PithView* pith_view_vstack(PithView **children, size_t count) {
    PithView *view = malloc(sizeof(PithView));
    memset(view, 0, sizeof(PithView));
    view->type = VIEW_VSTACK;
    view->as.stack.children = malloc(count * sizeof(PithView*));
    memcpy(view->as.stack.children, children, count * sizeof(PithView*));
    view->as.stack.count = count;
    return view;
}

PithView* pith_view_hstack(PithView **children, size_t count) {
    PithView *view = malloc(sizeof(PithView));
    memset(view, 0, sizeof(PithView));
    view->type = VIEW_HSTACK;
    view->as.stack.children = malloc(count * sizeof(PithView*));
    memcpy(view->as.stack.children, children, count * sizeof(PithView*));
    view->as.stack.count = count;
    return view;
}

void pith_view_free(PithView *view) {
    if (!view) return;
    
    switch (view->type) {
        case VIEW_TEXT:
            free(view->as.text.content);
            break;
        case VIEW_TEXTFIELD:
            pith_gapbuf_free(view->as.textfield.buffer);
            break;
        case VIEW_BUTTON:
            free(view->as.button.label);
            break;
        case VIEW_TEXTURE:
            free(view->as.texture.path);
            break;
        case VIEW_VSTACK:
        case VIEW_HSTACK:
            for (size_t i = 0; i < view->as.stack.count; i++) {
                pith_view_free(view->as.stack.children[i]);
            }
            free(view->as.stack.children);
            break;
        case VIEW_SPACER:
            /* Spacer has no data to free */
            break;
    }
    
    if (view->style.has_border) {
        free(view->style.border);
    }

    free(view);
}

/* Apply style slots from a dictionary (following parent chain) to a view */
static void pith_apply_dict_styles(PithDict *dict, PithView *view) {
    if (!dict || !view) return;

    /* Look up each style slot and apply if found */

    /* Color (text color) */
    PithSlot *color_slot = pith_dict_lookup(dict, "color");
    if (color_slot && color_slot->is_cached &&
        color_slot->cached.type == VAL_STRING) {
        view->style.has_color = true;
        view->style.color = pith_color_parse(color_slot->cached.as.string);
    }

    /* Background */
    PithSlot *bg_slot = pith_dict_lookup(dict, "background");
    if (bg_slot && bg_slot->is_cached &&
        bg_slot->cached.type == VAL_STRING) {
        const char *bg_str = bg_slot->cached.as.string;
        if (strcmp(bg_str, "none") == 0 || strcmp(bg_str, "transparent") == 0) {
            view->style.has_background = false;
        } else {
            view->style.has_background = true;
            view->style.background = pith_color_parse(bg_str);
        }
    }

    /* Padding */
    PithSlot *padding_slot = pith_dict_lookup(dict, "padding");
    if (padding_slot && padding_slot->is_cached &&
        padding_slot->cached.type == VAL_NUMBER) {
        view->style.has_padding = true;
        view->style.padding = (int)padding_slot->cached.as.number;
    }

    /* Gap */
    PithSlot *gap_slot = pith_dict_lookup(dict, "gap");
    if (gap_slot && gap_slot->is_cached &&
        gap_slot->cached.type == VAL_NUMBER) {
        view->style.has_gap = true;
        view->style.gap = (int)gap_slot->cached.as.number;
    }

    /* Border */
    PithSlot *border_slot = pith_dict_lookup(dict, "border");
    if (border_slot && border_slot->is_cached &&
        border_slot->cached.type == VAL_STRING) {
        view->style.has_border = true;
        if (view->style.border) free(view->style.border);
        view->style.border = pith_strdup(border_slot->cached.as.string);
    }

    /* Bold */
    PithSlot *bold_slot = pith_dict_lookup(dict, "bold");
    if (bold_slot && bold_slot->is_cached &&
        bold_slot->cached.type == VAL_BOOL) {
        view->style.has_bold = true;
        view->style.bold = bold_slot->cached.as.boolean;
    }

    /* Fill */
    PithSlot *fill_slot = pith_dict_lookup(dict, "fill");
    if (fill_slot && fill_slot->is_cached &&
        fill_slot->cached.type == VAL_BOOL) {
        view->style.fill = fill_slot->cached.as.boolean;
    }
}

/* ========================================================================
   LEXER
   ======================================================================== */

typedef struct {
    const char *source;
    const char *current;
    size_t line;
    size_t column;
} Lexer;

static void lexer_init(Lexer *lex, const char *source) {
    lex->source = source;
    lex->current = source;
    lex->line = 1;
    lex->column = 1;
}

static char lexer_peek(Lexer *lex) {
    return *lex->current;
}

static char lexer_advance(Lexer *lex) {
    char c = *lex->current++;
    if (c == '\n') {
        lex->line++;
        lex->column = 1;
    } else {
        lex->column++;
    }
    return c;
}

static void lexer_skip_whitespace(Lexer *lex) {
    while (1) {
        char c = lexer_peek(lex);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',') {
            lexer_advance(lex);
        } else if (c == '#') {
            /* Skip comment until end of line */
            while (lexer_peek(lex) != '\0' && lexer_peek(lex) != '\n') {
                lexer_advance(lex);
            }
        } else {
            break;
        }
    }
}

static bool is_word_char(char c) {
    return isalnum(c) || c == '_' || c == '-' || c == '?' || c == '!' ||
           c == '=' || c == '<' || c == '>' || c == '+' || c == '*' || c == '/';
}

static PithToken lexer_next(Lexer *lex, PithRuntime *rt) {
    lexer_skip_whitespace(lex);
    
    PithToken token = {0};
    token.line = lex->line;
    token.column = lex->column;
    
    char c = lexer_peek(lex);
    
    if (c == '\0') {
        token.type = TOK_EOF;
        return token;
    }
    
    /* Single character tokens */
    if (c == ':') { lexer_advance(lex); token.type = TOK_COLON; return token; }
    if (c == '.') { lexer_advance(lex); token.type = TOK_DOT; return token; }
    if (c == '[') { lexer_advance(lex); token.type = TOK_LBRACKET; return token; }
    if (c == ']') { lexer_advance(lex); token.type = TOK_RBRACKET; return token; }
    if (c == '{') { lexer_advance(lex); token.type = TOK_LBRACE; return token; }
    if (c == '}') { lexer_advance(lex); token.type = TOK_RBRACE; return token; }
    
    /* String */
    if (c == '"') {
        lexer_advance(lex); /* consume opening quote */
        /* First pass: count length needed */
        const char *scan = lex->current;
        size_t len = 0;
        while (*scan && *scan != '"') {
            if (*scan == '\\' && scan[1]) {
                scan += 2;
            } else {
                scan++;
            }
            len++;
        }
        /* Second pass: copy with escape processing */
        token.text = malloc(len + 1);
        size_t i = 0;
        while (lexer_peek(lex) != '\0' && lexer_peek(lex) != '"') {
            char ch = lexer_peek(lex);
            lexer_advance(lex);
            if (ch == '\\' && lexer_peek(lex) != '\0') {
                char esc = lexer_peek(lex);
                lexer_advance(lex);
                switch (esc) {
                    case 'n': ch = '\n'; break;
                    case 't': ch = '\t'; break;
                    case 'r': ch = '\r'; break;
                    case '\\': ch = '\\'; break;
                    case '"': ch = '"'; break;
                    default: ch = esc; break;
                }
            }
            token.text[i++] = ch;
        }
        token.text[i] = '\0';
        token.type = TOK_STRING;
        if (lexer_peek(lex) == '"') lexer_advance(lex); /* consume closing quote */
        return token;
    }
    
    /* Number */
    if (isdigit(c) || (c == '-' && isdigit(lex->current[1]))) {
        const char *start = lex->current;
        if (c == '-') lexer_advance(lex);
        while (isdigit(lexer_peek(lex))) lexer_advance(lex);
        if (lexer_peek(lex) == '.') {
            lexer_advance(lex);
            while (isdigit(lexer_peek(lex))) lexer_advance(lex);
        }
        size_t len = lex->current - start;
        token.text = malloc(len + 1);
        memcpy(token.text, start, len);
        token.text[len] = '\0';
        token.type = TOK_NUMBER;
        return token;
    }
    
    /* Word or keyword */
    if (is_word_char(c)) {
        const char *start = lex->current;
        while (is_word_char(lexer_peek(lex))) lexer_advance(lex);
        size_t len = lex->current - start;
        token.text = malloc(len + 1);
        memcpy(token.text, start, len);
        token.text[len] = '\0';
        
        /* Check for keywords */
        if (strcmp(token.text, "end") == 0) token.type = TOK_END;
        else if (strcmp(token.text, "if") == 0) token.type = TOK_IF;
        else if (strcmp(token.text, "else") == 0) token.type = TOK_ELSE;
        else if (strcmp(token.text, "do") == 0) token.type = TOK_DO;
        else if (strcmp(token.text, "true") == 0) token.type = TOK_TRUE;
        else if (strcmp(token.text, "false") == 0) token.type = TOK_FALSE;
        else if (strcmp(token.text, "nil") == 0) token.type = TOK_NIL;
        else token.type = TOK_WORD;
        
        return token;
    }
    
    /* Unknown character */
    pith_error(rt, "Unexpected character '%c' at line %zu", c, lex->line);
    lexer_advance(lex);
    return lexer_next(lex, rt);
}

/* ========================================================================
   PARSER
   ======================================================================== */

static bool pith_parse(PithRuntime *rt, const char *source) {
    Lexer lex;
    lexer_init(&lex, source);
    
    rt->token_count = 0;
    
    while (1) {
        if (rt->token_count >= PITH_TOKEN_MAX) {
            pith_error(rt, "Too many tokens");
            return false;
        }
        
        PithToken token = lexer_next(&lex, rt);
        rt->tokens[rt->token_count++] = token;
        
        if (token.type == TOK_EOF) break;
        if (rt->has_error) return false;
    }
    
    return true;
}

/* ========================================================================
   BUILT-IN WORDS
   ======================================================================== */

/* Stack operations */
static bool builtin_dup(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    return pith_push(rt, pith_value_copy(pith_peek(rt)));
}

static bool builtin_drop(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    pith_value_free(pith_pop(rt));
    return true;
}

static bool builtin_swap(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    pith_push(rt, b);
    pith_push(rt, a);
    return true;
}

static bool builtin_over(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue copy = pith_value_copy(rt->stack[rt->stack_top - 2]);
    return pith_push(rt, copy);
}

static bool builtin_rot(PithRuntime *rt) {
    /* ( a b c -- b c a ) - rotate top three elements */
    if (!pith_stack_has(rt, 3)) return false;
    PithValue c = rt->stack[rt->stack_top - 1];
    PithValue b = rt->stack[rt->stack_top - 2];
    PithValue a = rt->stack[rt->stack_top - 3];
    rt->stack[rt->stack_top - 3] = b;
    rt->stack[rt->stack_top - 2] = c;
    rt->stack[rt->stack_top - 1] = a;
    return true;
}

/* Arithmetic */
static bool builtin_add(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    if (PITH_IS_NUMBER(a) && PITH_IS_NUMBER(b)) {
        return pith_push(rt, PITH_NUMBER(a.as.number + b.as.number));
    }
    if (PITH_IS_STRING(a) && PITH_IS_STRING(b)) {
        size_t len = strlen(a.as.string) + strlen(b.as.string) + 1;
        char *result = malloc(len);
        strcpy(result, a.as.string);
        strcat(result, b.as.string);
        pith_value_free(a);
        pith_value_free(b);
        return pith_push(rt, PITH_STRING(result));
    }
    pith_error(rt, "Cannot add values of these types");
    return false;
}

static bool builtin_subtract(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    if (!PITH_IS_NUMBER(a) || !PITH_IS_NUMBER(b)) {
        pith_error(rt, "subtract requires numbers");
        return false;
    }
    return pith_push(rt, PITH_NUMBER(a.as.number - b.as.number));
}

static bool builtin_multiply(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    if (!PITH_IS_NUMBER(a) || !PITH_IS_NUMBER(b)) {
        pith_error(rt, "multiply requires numbers");
        return false;
    }
    return pith_push(rt, PITH_NUMBER(a.as.number * b.as.number));
}

static bool builtin_divide(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    if (!PITH_IS_NUMBER(a) || !PITH_IS_NUMBER(b)) {
        pith_error(rt, "divide requires numbers");
        return false;
    }
    if (b.as.number == 0) {
        pith_error(rt, "division by zero");
        return false;
    }
    return pith_push(rt, PITH_NUMBER(a.as.number / b.as.number));
}

static bool builtin_mod(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    if (!PITH_IS_NUMBER(a) || !PITH_IS_NUMBER(b)) {
        pith_error(rt, "mod requires numbers");
        return false;
    }
    if (b.as.number == 0) {
        pith_error(rt, "modulo by zero");
        return false;
    }
    return pith_push(rt, PITH_NUMBER(fmod(a.as.number, b.as.number)));
}

static bool builtin_abs(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    if (!PITH_IS_NUMBER(a)) {
        pith_error(rt, "abs requires number");
        return false;
    }
    return pith_push(rt, PITH_NUMBER(fabs(a.as.number)));
}

static bool builtin_min(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    if (!PITH_IS_NUMBER(a) || !PITH_IS_NUMBER(b)) {
        pith_error(rt, "min requires numbers");
        return false;
    }
    return pith_push(rt, PITH_NUMBER(a.as.number < b.as.number ? a.as.number : b.as.number));
}

static bool builtin_max(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    if (!PITH_IS_NUMBER(a) || !PITH_IS_NUMBER(b)) {
        pith_error(rt, "max requires numbers");
        return false;
    }
    return pith_push(rt, PITH_NUMBER(a.as.number > b.as.number ? a.as.number : b.as.number));
}

/* Comparison */
static bool builtin_equal(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    bool result = pith_value_equal(a, b);
    pith_value_free(a);
    pith_value_free(b);
    return pith_push(rt, PITH_BOOL(result));
}

static bool builtin_less(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    if (!PITH_IS_NUMBER(a) || !PITH_IS_NUMBER(b)) {
        pith_error(rt, "< requires numbers");
        return false;
    }
    return pith_push(rt, PITH_BOOL(a.as.number < b.as.number));
}

static bool builtin_greater(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    if (!PITH_IS_NUMBER(a) || !PITH_IS_NUMBER(b)) {
        pith_error(rt, "> requires numbers");
        return false;
    }
    return pith_push(rt, PITH_BOOL(a.as.number > b.as.number));
}

static bool builtin_not_equal(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    bool result = !pith_value_equal(a, b);
    pith_value_free(a);
    pith_value_free(b);
    return pith_push(rt, PITH_BOOL(result));
}

static bool builtin_less_equal(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    if (!PITH_IS_NUMBER(a) || !PITH_IS_NUMBER(b)) {
        pith_error(rt, "<= requires numbers");
        return false;
    }
    return pith_push(rt, PITH_BOOL(a.as.number <= b.as.number));
}

static bool builtin_greater_equal(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    if (!PITH_IS_NUMBER(a) || !PITH_IS_NUMBER(b)) {
        pith_error(rt, ">= requires numbers");
        return false;
    }
    return pith_push(rt, PITH_BOOL(a.as.number >= b.as.number));
}

/* Logic */
static bool builtin_and(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    return pith_push(rt, PITH_BOOL(a.as.boolean && b.as.boolean));
}

static bool builtin_or(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    return pith_push(rt, PITH_BOOL(a.as.boolean || b.as.boolean));
}

static bool builtin_not(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    return pith_push(rt, PITH_BOOL(!a.as.boolean));
}

/* String operations */
static bool builtin_length(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    if (PITH_IS_STRING(a)) {
        size_t len = strlen(a.as.string);
        pith_value_free(a);
        return pith_push(rt, PITH_NUMBER((double)len));
    }
    if (PITH_IS_ARRAY(a)) {
        size_t len = a.as.array->length;
        pith_value_free(a);
        return pith_push(rt, PITH_NUMBER((double)len));
    }
    pith_error(rt, "length requires string or array");
    return false;
}

/* String Operations */
static bool builtin_concat(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue b = pith_pop(rt);
    PithValue a = pith_pop(rt);
    if (!PITH_IS_STRING(a) || !PITH_IS_STRING(b)) {
        pith_error(rt, "concat requires two strings");
        pith_value_free(a);
        pith_value_free(b);
        return false;
    }
    size_t len_a = strlen(a.as.string);
    size_t len_b = strlen(b.as.string);
    char *result = malloc(len_a + len_b + 1);
    memcpy(result, a.as.string, len_a);
    memcpy(result + len_a, b.as.string, len_b + 1);
    pith_value_free(a);
    pith_value_free(b);
    return pith_push(rt, PITH_STRING(result));
}

static bool builtin_split(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue delim = pith_pop(rt);
    PithValue str = pith_pop(rt);
    if (!PITH_IS_STRING(str) || !PITH_IS_STRING(delim)) {
        pith_error(rt, "split requires string and delimiter");
        pith_value_free(str);
        pith_value_free(delim);
        return false;
    }
    PithArray *array = pith_array_new();
    char *copy = pith_strdup(str.as.string);
    char *token = copy;
    char *next;
    size_t delim_len = strlen(delim.as.string);

    if (delim_len == 0) {
        // Split into individual characters
        for (size_t i = 0; copy[i]; i++) {
            char *ch = malloc(2);
            ch[0] = copy[i];
            ch[1] = '\0';
            pith_array_push(array, PITH_STRING(ch));
        }
    } else {
        while ((next = strstr(token, delim.as.string)) != NULL) {
            *next = '\0';
            pith_array_push(array, PITH_STRING(pith_strdup(token)));
            token = next + delim_len;
        }
        pith_array_push(array, PITH_STRING(pith_strdup(token)));
    }

    free(copy);
    pith_value_free(str);
    pith_value_free(delim);
    return pith_push(rt, PITH_ARRAY(array));
}

static bool builtin_join(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue delim = pith_pop(rt);
    PithValue arr = pith_pop(rt);
    if (!PITH_IS_ARRAY(arr) || !PITH_IS_STRING(delim)) {
        pith_error(rt, "join requires array and delimiter");
        pith_value_free(arr);
        pith_value_free(delim);
        return false;
    }

    // Calculate total length
    size_t total = 0;
    size_t delim_len = strlen(delim.as.string);
    for (size_t i = 0; i < arr.as.array->length; i++) {
        if (PITH_IS_STRING(arr.as.array->items[i])) {
            total += strlen(arr.as.array->items[i].as.string);
        }
        if (i > 0) total += delim_len;
    }

    char *result = malloc(total + 1);
    result[0] = '\0';
    size_t pos = 0;

    for (size_t i = 0; i < arr.as.array->length; i++) {
        if (i > 0) {
            memcpy(result + pos, delim.as.string, delim_len);
            pos += delim_len;
        }
        if (PITH_IS_STRING(arr.as.array->items[i])) {
            size_t len = strlen(arr.as.array->items[i].as.string);
            memcpy(result + pos, arr.as.array->items[i].as.string, len);
            pos += len;
        }
    }
    result[pos] = '\0';

    pith_value_free(arr);
    pith_value_free(delim);
    return pith_push(rt, PITH_STRING(result));
}

static bool builtin_trim(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue str = pith_pop(rt);
    if (!PITH_IS_STRING(str)) {
        pith_error(rt, "trim requires a string");
        pith_value_free(str);
        return false;
    }

    const char *start = str.as.string;
    while (*start && isspace((unsigned char)*start)) start++;

    const char *end = str.as.string + strlen(str.as.string) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;

    size_t len = end - start + 1;
    char *result = malloc(len + 1);
    memcpy(result, start, len);
    result[len] = '\0';

    pith_value_free(str);
    return pith_push(rt, PITH_STRING(result));
}

static bool builtin_substring(PithRuntime *rt) {
    if (!pith_stack_has(rt, 3)) return false;
    PithValue end_val = pith_pop(rt);
    PithValue start_val = pith_pop(rt);
    PithValue str = pith_pop(rt);

    if (!PITH_IS_STRING(str) || !PITH_IS_NUMBER(start_val) || !PITH_IS_NUMBER(end_val)) {
        pith_error(rt, "substring requires string, start, end");
        pith_value_free(str);
        pith_value_free(start_val);
        pith_value_free(end_val);
        return false;
    }

    size_t len = strlen(str.as.string);
    int start = (int)start_val.as.number;
    int end = (int)end_val.as.number;

    // Handle negative indices
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if ((size_t)start > len) start = len;
    if ((size_t)end > len) end = len;
    if (start > end) start = end;

    size_t sub_len = end - start;
    char *result = malloc(sub_len + 1);
    memcpy(result, str.as.string + start, sub_len);
    result[sub_len] = '\0';

    pith_value_free(str);
    return pith_push(rt, PITH_STRING(result));
}

static bool builtin_contains(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue search = pith_pop(rt);
    PithValue container = pith_pop(rt);

    if (PITH_IS_STRING(container) && PITH_IS_STRING(search)) {
        bool found = strstr(container.as.string, search.as.string) != NULL;
        pith_value_free(container);
        pith_value_free(search);
        return pith_push(rt, PITH_BOOL(found));
    }

    if (PITH_IS_ARRAY(container)) {
        bool found = false;
        for (size_t i = 0; i < container.as.array->length; i++) {
            if (pith_value_equal(container.as.array->items[i], search)) {
                found = true;
                break;
            }
        }
        pith_value_free(container);
        pith_value_free(search);
        return pith_push(rt, PITH_BOOL(found));
    }

    pith_error(rt, "contains requires string or array");
    pith_value_free(container);
    pith_value_free(search);
    return false;
}

static bool builtin_replace(PithRuntime *rt) {
    if (!pith_stack_has(rt, 3)) return false;
    PithValue new_str = pith_pop(rt);
    PithValue old_str = pith_pop(rt);
    PithValue str = pith_pop(rt);

    if (!PITH_IS_STRING(str) || !PITH_IS_STRING(old_str) || !PITH_IS_STRING(new_str)) {
        pith_error(rt, "replace requires three strings");
        pith_value_free(str);
        pith_value_free(old_str);
        pith_value_free(new_str);
        return false;
    }

    size_t old_len = strlen(old_str.as.string);
    size_t new_len = strlen(new_str.as.string);

    if (old_len == 0) {
        // Can't replace empty string
        char *result = pith_strdup(str.as.string);
        pith_value_free(str);
        pith_value_free(old_str);
        pith_value_free(new_str);
        return pith_push(rt, PITH_STRING(result));
    }

    // Count occurrences
    size_t count = 0;
    const char *p = str.as.string;
    while ((p = strstr(p, old_str.as.string)) != NULL) {
        count++;
        p += old_len;
    }

    // Allocate result
    size_t result_len = strlen(str.as.string) + count * (new_len - old_len);
    char *result = malloc(result_len + 1);

    // Build result
    char *dest = result;
    p = str.as.string;
    const char *found;
    while ((found = strstr(p, old_str.as.string)) != NULL) {
        size_t prefix_len = found - p;
        memcpy(dest, p, prefix_len);
        dest += prefix_len;
        memcpy(dest, new_str.as.string, new_len);
        dest += new_len;
        p = found + old_len;
    }
    strcpy(dest, p);

    pith_value_free(str);
    pith_value_free(old_str);
    pith_value_free(new_str);
    return pith_push(rt, PITH_STRING(result));
}

static bool builtin_uppercase(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue str = pith_pop(rt);
    if (!PITH_IS_STRING(str)) {
        pith_error(rt, "uppercase requires a string");
        pith_value_free(str);
        return false;
    }

    char *result = pith_strdup(str.as.string);
    for (char *p = result; *p; p++) {
        *p = toupper((unsigned char)*p);
    }

    pith_value_free(str);
    return pith_push(rt, PITH_STRING(result));
}

static bool builtin_lowercase(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue str = pith_pop(rt);
    if (!PITH_IS_STRING(str)) {
        pith_error(rt, "lowercase requires a string");
        pith_value_free(str);
        return false;
    }

    char *result = pith_strdup(str.as.string);
    for (char *p = result; *p; p++) {
        *p = tolower((unsigned char)*p);
    }

    pith_value_free(str);
    return pith_push(rt, PITH_STRING(result));
}

/* Text Parsing */
static bool builtin_lines(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue str = pith_pop(rt);
    if (!PITH_IS_STRING(str)) {
        pith_error(rt, "lines requires a string");
        pith_value_free(str);
        return false;
    }

    PithArray *array = pith_array_new();
    char *copy = pith_strdup(str.as.string);
    char *line = copy;
    char *next;

    while ((next = strchr(line, '\n')) != NULL) {
        *next = '\0';
        pith_array_push(array, PITH_STRING(pith_strdup(line)));
        line = next + 1;
    }
    // Add the last line (or only line if no newlines)
    if (*line || array->length == 0) {
        pith_array_push(array, PITH_STRING(pith_strdup(line)));
    }

    free(copy);
    pith_value_free(str);
    return pith_push(rt, PITH_ARRAY(array));
}

static bool builtin_words(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue str = pith_pop(rt);
    if (!PITH_IS_STRING(str)) {
        pith_error(rt, "words requires a string");
        pith_value_free(str);
        return false;
    }

    PithArray *array = pith_array_new();
    char *copy = pith_strdup(str.as.string);
    char *p = copy;

    while (*p) {
        // Skip whitespace
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        // Find end of word
        char *start = p;
        while (*p && !isspace((unsigned char)*p)) p++;

        // Extract word
        size_t len = p - start;
        char *word = malloc(len + 1);
        memcpy(word, start, len);
        word[len] = '\0';
        pith_array_push(array, PITH_STRING(word));
    }

    free(copy);
    pith_value_free(str);
    return pith_push(rt, PITH_ARRAY(array));
}

/* Type Checking */
static bool builtin_type(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    const char *type_name;
    switch (a.type) {
        case VAL_NIL: type_name = "nil"; break;
        case VAL_BOOL: type_name = "bool"; break;
        case VAL_NUMBER: type_name = "number"; break;
        case VAL_STRING: type_name = "string"; break;
        case VAL_ARRAY: type_name = "array"; break;
        case VAL_VIEW: type_name = "view"; break;
        case VAL_DICT: type_name = "dict"; break;
        case VAL_BLOCK: type_name = "block"; break;
        case VAL_GAPBUF: type_name = "gapbuf"; break;
        default: type_name = "unknown"; break;
    }
    pith_value_free(a);
    return pith_push(rt, PITH_STRING(pith_strdup(type_name)));
}

static bool builtin_is_string(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    bool result = PITH_IS_STRING(a);
    pith_value_free(a);
    return pith_push(rt, PITH_BOOL(result));
}

static bool builtin_is_number(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    bool result = PITH_IS_NUMBER(a);
    pith_value_free(a);
    return pith_push(rt, PITH_BOOL(result));
}

static bool builtin_is_array(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    bool result = PITH_IS_ARRAY(a);
    pith_value_free(a);
    return pith_push(rt, PITH_BOOL(result));
}

static bool builtin_is_map(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    bool result = (a.type == VAL_DICT);
    pith_value_free(a);
    return pith_push(rt, PITH_BOOL(result));
}

static bool builtin_is_bool(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    bool result = PITH_IS_BOOL(a);
    pith_value_free(a);
    return pith_push(rt, PITH_BOOL(result));
}

static bool builtin_is_nil(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    bool result = PITH_IS_NIL(a);
    pith_value_free(a);
    return pith_push(rt, PITH_BOOL(result));
}

/* Type Conversion */
static bool builtin_to_string(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    char *str = pith_value_to_string(a);
    pith_value_free(a);
    return pith_push(rt, PITH_STRING(str));
}

static bool builtin_to_number(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);

    if (PITH_IS_NUMBER(a)) {
        return pith_push(rt, a);
    }

    if (PITH_IS_STRING(a)) {
        char *endptr;
        double num = strtod(a.as.string, &endptr);
        bool valid = (*endptr == '\0');
        pith_value_free(a);
        if (!valid) {
            return pith_push(rt, PITH_NIL());
        }
        return pith_push(rt, PITH_NUMBER(num));
    }

    if (PITH_IS_BOOL(a)) {
        return pith_push(rt, PITH_NUMBER(a.as.boolean ? 1.0 : 0.0));
    }

    pith_value_free(a);
    return pith_push(rt, PITH_NIL());
}

/* Map Operations (using Dict as the underlying structure) */
static bool builtin_map_new(PithRuntime *rt) {
    PithDict *dict = pith_dict_new(NULL);
    return pith_push(rt, PITH_DICT(dict));
}

static bool builtin_map_get(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue key = pith_pop(rt);
    PithValue map = pith_pop(rt);

    if (map.type != VAL_DICT) {
        pith_error(rt, "get requires a map");
        pith_value_free(map);
        pith_value_free(key);
        return false;
    }
    if (!PITH_IS_STRING(key)) {
        pith_error(rt, "get requires string key");
        pith_value_free(map);
        pith_value_free(key);
        return false;
    }

    PithSlot *slot = pith_dict_lookup(map.as.dict, key.as.string);
    PithValue result = PITH_NIL();
    if (slot && slot->is_cached) {
        result = pith_value_copy(slot->cached);
    }

    pith_value_free(key);
    /* Don't free the map - it might be referenced elsewhere */
    return pith_push(rt, result);
}

static bool builtin_map_set(PithRuntime *rt) {
    if (!pith_stack_has(rt, 3)) return false;
    PithValue key = pith_pop(rt);
    PithValue map = pith_pop(rt);
    PithValue value = pith_pop(rt);

    if (map.type != VAL_DICT) {
        pith_error(rt, "set requires a map");
        pith_value_free(map);
        pith_value_free(key);
        pith_value_free(value);
        return false;
    }
    if (!PITH_IS_STRING(key)) {
        pith_error(rt, "set requires string key");
        pith_value_free(map);
        pith_value_free(key);
        pith_value_free(value);
        return false;
    }

    /* Create a copy with the new value */
    PithDict *new_dict = pith_dict_copy(map.as.dict);
    pith_dict_set_value(new_dict, key.as.string, value);

    pith_value_free(key);
    return pith_push(rt, PITH_DICT(new_dict));
}

static bool builtin_map_keys(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue map = pith_pop(rt);

    if (map.type != VAL_DICT) {
        pith_error(rt, "keys requires a map");
        pith_value_free(map);
        return false;
    }

    PithArray *array = pith_array_new();
    PithDict *dict = map.as.dict;
    for (size_t i = 0; i < dict->slot_count; i++) {
        pith_array_push(array, PITH_STRING(pith_strdup(dict->slots[i].name)));
    }

    return pith_push(rt, PITH_ARRAY(array));
}

static bool builtin_map_values(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue map = pith_pop(rt);

    if (map.type != VAL_DICT) {
        pith_error(rt, "values requires a map");
        pith_value_free(map);
        return false;
    }

    PithArray *array = pith_array_new();
    PithDict *dict = map.as.dict;
    for (size_t i = 0; i < dict->slot_count; i++) {
        if (dict->slots[i].is_cached) {
            pith_array_push(array, pith_value_copy(dict->slots[i].cached));
        } else {
            pith_array_push(array, PITH_NIL());
        }
    }

    return pith_push(rt, PITH_ARRAY(array));
}

static bool builtin_map_has(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue key = pith_pop(rt);
    PithValue map = pith_pop(rt);

    if (map.type != VAL_DICT) {
        pith_error(rt, "has requires a map");
        pith_value_free(map);
        pith_value_free(key);
        return false;
    }
    if (!PITH_IS_STRING(key)) {
        pith_error(rt, "has requires string key");
        pith_value_free(map);
        pith_value_free(key);
        return false;
    }

    PithSlot *slot = pith_dict_lookup(map.as.dict, key.as.string);
    bool exists = (slot != NULL);

    pith_value_free(key);
    return pith_push(rt, PITH_BOOL(exists));
}

static bool builtin_map_remove(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue key = pith_pop(rt);
    PithValue map = pith_pop(rt);

    if (map.type != VAL_DICT) {
        pith_error(rt, "remove requires a map");
        pith_value_free(map);
        pith_value_free(key);
        return false;
    }
    if (!PITH_IS_STRING(key)) {
        pith_error(rt, "remove requires string key");
        pith_value_free(map);
        pith_value_free(key);
        return false;
    }

    /* Create a copy without the key */
    PithDict *new_dict = pith_dict_copy(map.as.dict);
    pith_dict_remove_slot(new_dict, key.as.string);

    pith_value_free(key);
    return pith_push(rt, PITH_DICT(new_dict));
}

static bool builtin_map_merge(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue map2 = pith_pop(rt);
    PithValue map1 = pith_pop(rt);

    if (map1.type != VAL_DICT || map2.type != VAL_DICT) {
        pith_error(rt, "merge requires two maps");
        pith_value_free(map1);
        pith_value_free(map2);
        return false;
    }

    /* Create a copy of map1, then add all slots from map2 */
    PithDict *new_dict = pith_dict_copy(map1.as.dict);
    PithDict *src = map2.as.dict;
    for (size_t i = 0; i < src->slot_count; i++) {
        PithSlot *s = &src->slots[i];
        if (s->is_cached) {
            pith_dict_set_value(new_dict, s->name, pith_value_copy(s->cached));
        }
    }

    return pith_push(rt, PITH_DICT(new_dict));
}

/* Forward declaration for recursive sanitize */
static PithValue pith_value_sanitize(PithValue value);

static PithDict* pith_dict_sanitize(PithDict *src) {
    PithDict *copy = pith_dict_new(src->name);
    /* Don't copy parent - sanitized dict is standalone data */

    for (size_t i = 0; i < src->slot_count; i++) {
        PithSlot *s = &src->slots[i];
        /* Only copy slots that have cached values (pure data) */
        if (s->is_cached) {
            PithValue sanitized = pith_value_sanitize(s->cached);
            pith_dict_set_value(copy, s->name, sanitized);
        }
        /* Skip slots with executable code (body_start/body_end) */
    }

    return copy;
}

static PithArray* pith_array_sanitize(PithArray *src) {
    PithArray *copy = pith_array_new();
    for (size_t i = 0; i < src->length; i++) {
        pith_array_push(copy, pith_value_sanitize(src->items[i]));
    }
    return copy;
}

static PithValue pith_value_sanitize(PithValue value) {
    switch (value.type) {
        case VAL_DICT:
            return PITH_DICT(pith_dict_sanitize(value.as.dict));
        case VAL_ARRAY:
            return PITH_ARRAY(pith_array_sanitize(value.as.array));
        case VAL_BLOCK:
            /* Blocks are executable - return nil */
            return PITH_NIL();
        case VAL_VIEW:
            /* Views are runtime objects - return nil */
            return PITH_NIL();
        default:
            /* Primitives: nil, bool, number, string - copy as-is */
            return pith_value_copy(value);
    }
}

static bool builtin_sanitize(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue value = pith_pop(rt);

    PithValue result = pith_value_sanitize(value);
    pith_value_free(value);

    return pith_push(rt, result);
}

/* JSON Serialization */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} JsonBuffer;

static void json_buf_init(JsonBuffer *jb) {
    jb->cap = 256;
    jb->buf = malloc(jb->cap);
    jb->buf[0] = '\0';
    jb->len = 0;
}

static void json_buf_append(JsonBuffer *jb, const char *str) {
    size_t slen = strlen(str);
    while (jb->len + slen + 1 > jb->cap) {
        jb->cap *= 2;
        jb->buf = realloc(jb->buf, jb->cap);
    }
    memcpy(jb->buf + jb->len, str, slen + 1);
    jb->len += slen;
}

static void json_buf_append_char(JsonBuffer *jb, char c) {
    if (jb->len + 2 > jb->cap) {
        jb->cap *= 2;
        jb->buf = realloc(jb->buf, jb->cap);
    }
    jb->buf[jb->len++] = c;
    jb->buf[jb->len] = '\0';
}

static void json_serialize_value(JsonBuffer *jb, PithValue value);

static void json_serialize_string(JsonBuffer *jb, const char *str) {
    json_buf_append_char(jb, '"');
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '"':  json_buf_append(jb, "\\\""); break;
            case '\\': json_buf_append(jb, "\\\\"); break;
            case '\b': json_buf_append(jb, "\\b"); break;
            case '\f': json_buf_append(jb, "\\f"); break;
            case '\n': json_buf_append(jb, "\\n"); break;
            case '\r': json_buf_append(jb, "\\r"); break;
            case '\t': json_buf_append(jb, "\\t"); break;
            default:
                if ((unsigned char)*p < 32) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)*p);
                    json_buf_append(jb, esc);
                } else {
                    json_buf_append_char(jb, *p);
                }
        }
    }
    json_buf_append_char(jb, '"');
}

static void json_serialize_array(JsonBuffer *jb, PithArray *arr) {
    json_buf_append_char(jb, '[');
    for (size_t i = 0; i < arr->length; i++) {
        if (i > 0) json_buf_append_char(jb, ',');
        json_serialize_value(jb, arr->items[i]);
    }
    json_buf_append_char(jb, ']');
}

static void json_serialize_dict(JsonBuffer *jb, PithDict *dict) {
    json_buf_append_char(jb, '{');
    bool first = true;
    for (size_t i = 0; i < dict->slot_count; i++) {
        PithSlot *s = &dict->slots[i];
        if (s->is_cached) {
            if (!first) json_buf_append_char(jb, ',');
            first = false;
            json_serialize_string(jb, s->name);
            json_buf_append_char(jb, ':');
            json_serialize_value(jb, s->cached);
        }
    }
    json_buf_append_char(jb, '}');
}

static void json_serialize_value(JsonBuffer *jb, PithValue value) {
    char numbuf[64];
    switch (value.type) {
        case VAL_NIL:
            json_buf_append(jb, "null");
            break;
        case VAL_BOOL:
            json_buf_append(jb, value.as.boolean ? "true" : "false");
            break;
        case VAL_NUMBER:
            snprintf(numbuf, sizeof(numbuf), "%g", value.as.number);
            json_buf_append(jb, numbuf);
            break;
        case VAL_STRING:
            json_serialize_string(jb, value.as.string);
            break;
        case VAL_ARRAY:
            json_serialize_array(jb, value.as.array);
            break;
        case VAL_DICT:
            json_serialize_dict(jb, value.as.dict);
            break;
        default:
            json_buf_append(jb, "null");
            break;
    }
}

static bool builtin_to_json(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue value = pith_pop(rt);

    if (value.type != VAL_DICT) {
        pith_error(rt, "to-json requires a map");
        pith_value_free(value);
        return false;
    }

    JsonBuffer jb;
    json_buf_init(&jb);
    json_serialize_dict(&jb, value.as.dict);

    pith_value_free(value);
    return pith_push(rt, PITH_STRING(jb.buf));
}

/* JSON Parsing */
typedef struct {
    const char *src;
    size_t pos;
    char error[256];
} JsonParser;

static void json_skip_ws(JsonParser *jp) {
    while (jp->src[jp->pos] && isspace((unsigned char)jp->src[jp->pos])) {
        jp->pos++;
    }
}

static PithValue json_parse_value(JsonParser *jp);

static PithValue json_parse_string(JsonParser *jp) {
    if (jp->src[jp->pos] != '"') {
        snprintf(jp->error, sizeof(jp->error), "Expected '\"'");
        return PITH_NIL();
    }
    jp->pos++; /* skip opening quote */

    size_t cap = 64;
    char *buf = malloc(cap);
    size_t len = 0;

    while (jp->src[jp->pos] && jp->src[jp->pos] != '"') {
        char c = jp->src[jp->pos++];
        if (c == '\\' && jp->src[jp->pos]) {
            char esc = jp->src[jp->pos++];
            switch (esc) {
                case '"':  c = '"'; break;
                case '\\': c = '\\'; break;
                case '/':  c = '/'; break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'u':
                    /* Simple unicode escape - just skip for now */
                    if (jp->src[jp->pos]) jp->pos++;
                    if (jp->src[jp->pos]) jp->pos++;
                    if (jp->src[jp->pos]) jp->pos++;
                    if (jp->src[jp->pos]) jp->pos++;
                    c = '?';
                    break;
                default: c = esc;
            }
        }
        if (len + 2 > cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        buf[len++] = c;
    }
    buf[len] = '\0';

    if (jp->src[jp->pos] == '"') {
        jp->pos++; /* skip closing quote */
    }

    return PITH_STRING(buf);
}

static PithValue json_parse_number(JsonParser *jp) {
    const char *start = jp->src + jp->pos;
    char *end;
    double num = strtod(start, &end);
    jp->pos += (end - start);
    return PITH_NUMBER(num);
}

static PithValue json_parse_array(JsonParser *jp) {
    jp->pos++; /* skip '[' */
    json_skip_ws(jp);

    PithArray *arr = pith_array_new();

    if (jp->src[jp->pos] == ']') {
        jp->pos++;
        return PITH_ARRAY(arr);
    }

    while (1) {
        json_skip_ws(jp);
        PithValue item = json_parse_value(jp);
        if (jp->error[0]) {
            pith_array_free(arr);
            return PITH_NIL();
        }
        pith_array_push(arr, item);

        json_skip_ws(jp);
        if (jp->src[jp->pos] == ']') {
            jp->pos++;
            break;
        }
        if (jp->src[jp->pos] == ',') {
            jp->pos++;
        } else {
            snprintf(jp->error, sizeof(jp->error), "Expected ',' or ']'");
            pith_array_free(arr);
            return PITH_NIL();
        }
    }

    return PITH_ARRAY(arr);
}

static PithValue json_parse_object(JsonParser *jp) {
    jp->pos++; /* skip '{' */
    json_skip_ws(jp);

    PithDict *dict = pith_dict_new(NULL);

    if (jp->src[jp->pos] == '}') {
        jp->pos++;
        return PITH_DICT(dict);
    }

    while (1) {
        json_skip_ws(jp);

        /* Parse key */
        if (jp->src[jp->pos] != '"') {
            snprintf(jp->error, sizeof(jp->error), "Expected string key");
            pith_dict_free(dict);
            return PITH_NIL();
        }
        PithValue key = json_parse_string(jp);
        if (jp->error[0]) {
            pith_dict_free(dict);
            return PITH_NIL();
        }

        json_skip_ws(jp);
        if (jp->src[jp->pos] != ':') {
            snprintf(jp->error, sizeof(jp->error), "Expected ':'");
            pith_value_free(key);
            pith_dict_free(dict);
            return PITH_NIL();
        }
        jp->pos++;

        json_skip_ws(jp);
        PithValue val = json_parse_value(jp);
        if (jp->error[0]) {
            pith_value_free(key);
            pith_dict_free(dict);
            return PITH_NIL();
        }

        pith_dict_set_value(dict, key.as.string, val);
        pith_value_free(key);

        json_skip_ws(jp);
        if (jp->src[jp->pos] == '}') {
            jp->pos++;
            break;
        }
        if (jp->src[jp->pos] == ',') {
            jp->pos++;
        } else {
            snprintf(jp->error, sizeof(jp->error), "Expected ',' or '}'");
            pith_dict_free(dict);
            return PITH_NIL();
        }
    }

    return PITH_DICT(dict);
}

static PithValue json_parse_value(JsonParser *jp) {
    json_skip_ws(jp);

    char c = jp->src[jp->pos];

    if (c == '"') {
        return json_parse_string(jp);
    }
    if (c == '[') {
        return json_parse_array(jp);
    }
    if (c == '{') {
        return json_parse_object(jp);
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        return json_parse_number(jp);
    }
    if (strncmp(jp->src + jp->pos, "true", 4) == 0) {
        jp->pos += 4;
        return PITH_BOOL(true);
    }
    if (strncmp(jp->src + jp->pos, "false", 5) == 0) {
        jp->pos += 5;
        return PITH_BOOL(false);
    }
    if (strncmp(jp->src + jp->pos, "null", 4) == 0) {
        jp->pos += 4;
        return PITH_NIL();
    }

    snprintf(jp->error, sizeof(jp->error), "Unexpected character '%c'", c);
    return PITH_NIL();
}

static bool builtin_parse_json(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue str = pith_pop(rt);

    if (!PITH_IS_STRING(str)) {
        pith_error(rt, "parse-json requires a string");
        pith_value_free(str);
        return false;
    }

    JsonParser jp = { .src = str.as.string, .pos = 0, .error = {0} };
    json_skip_ws(&jp);

    if (jp.src[jp.pos] != '{') {
        pith_error(rt, "parse-json requires JSON object at root");
        pith_value_free(str);
        return false;
    }

    PithValue result = json_parse_object(&jp);
    pith_value_free(str);

    if (jp.error[0]) {
        pith_error(rt, "JSON parse error: %s", jp.error);
        return false;
    }

    return pith_push(rt, result);
}

/* Gap Buffer Operations */
static bool builtin_gap_new(PithRuntime *rt) {
    return pith_push(rt, PITH_GAPBUF(pith_gapbuf_new()));
}

static bool builtin_string_to_gap(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue str = pith_pop(rt);

    if (!PITH_IS_STRING(str)) {
        pith_error(rt, "string>gap requires a string");
        pith_value_free(str);
        return false;
    }

    PithGapBuffer *gb = pith_gapbuf_from_string(str.as.string);
    pith_value_free(str);
    return pith_push(rt, PITH_GAPBUF(gb));
}

static bool builtin_gap_to_string(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue gb_val = pith_pop(rt);

    if (!PITH_IS_GAPBUF(gb_val)) {
        pith_error(rt, "gap>string requires a gap buffer");
        pith_value_free(gb_val);
        return false;
    }

    char *str = pith_gapbuf_to_string(gb_val.as.gapbuf);
    pith_value_free(gb_val);
    return pith_push(rt, PITH_STRING(str));
}

static bool builtin_gap_insert(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue gb_val = pith_pop(rt);
    PithValue str = pith_pop(rt);

    if (!PITH_IS_GAPBUF(gb_val)) {
        pith_error(rt, "gap-insert requires a gap buffer");
        pith_value_free(gb_val);
        pith_value_free(str);
        return false;
    }
    if (!PITH_IS_STRING(str)) {
        pith_error(rt, "gap-insert requires a string to insert");
        pith_value_free(gb_val);
        pith_value_free(str);
        return false;
    }

    PithGapBuffer *gb = pith_gapbuf_copy(gb_val.as.gapbuf);
    pith_gapbuf_insert(gb, str.as.string);
    pith_value_free(gb_val);
    pith_value_free(str);
    return pith_push(rt, PITH_GAPBUF(gb));
}

static bool builtin_gap_delete(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue gb_val = pith_pop(rt);
    PithValue n_val = pith_pop(rt);

    if (!PITH_IS_GAPBUF(gb_val)) {
        pith_error(rt, "gap-delete requires a gap buffer");
        pith_value_free(gb_val);
        pith_value_free(n_val);
        return false;
    }
    if (!PITH_IS_NUMBER(n_val)) {
        pith_error(rt, "gap-delete requires a number");
        pith_value_free(gb_val);
        pith_value_free(n_val);
        return false;
    }

    PithGapBuffer *gb = pith_gapbuf_copy(gb_val.as.gapbuf);
    pith_gapbuf_delete(gb, (int)n_val.as.number);
    pith_value_free(gb_val);
    return pith_push(rt, PITH_GAPBUF(gb));
}

static bool builtin_gap_move(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue gb_val = pith_pop(rt);
    PithValue n_val = pith_pop(rt);

    if (!PITH_IS_GAPBUF(gb_val)) {
        pith_error(rt, "gap-move requires a gap buffer");
        pith_value_free(gb_val);
        pith_value_free(n_val);
        return false;
    }
    if (!PITH_IS_NUMBER(n_val)) {
        pith_error(rt, "gap-move requires a number");
        pith_value_free(gb_val);
        pith_value_free(n_val);
        return false;
    }

    PithGapBuffer *gb = pith_gapbuf_copy(gb_val.as.gapbuf);
    pith_gapbuf_move(gb, (int)n_val.as.number);
    pith_value_free(gb_val);
    return pith_push(rt, PITH_GAPBUF(gb));
}

static bool builtin_gap_goto(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue gb_val = pith_pop(rt);
    PithValue pos_val = pith_pop(rt);

    if (!PITH_IS_GAPBUF(gb_val)) {
        pith_error(rt, "gap-goto requires a gap buffer");
        pith_value_free(gb_val);
        pith_value_free(pos_val);
        return false;
    }
    if (!PITH_IS_NUMBER(pos_val)) {
        pith_error(rt, "gap-goto requires a number");
        pith_value_free(gb_val);
        pith_value_free(pos_val);
        return false;
    }

    PithGapBuffer *gb = pith_gapbuf_copy(gb_val.as.gapbuf);
    pith_gapbuf_goto(gb, (size_t)pos_val.as.number);
    pith_value_free(gb_val);
    return pith_push(rt, PITH_GAPBUF(gb));
}

static bool builtin_gap_cursor(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue gb_val = pith_pop(rt);

    if (!PITH_IS_GAPBUF(gb_val)) {
        pith_error(rt, "gap-cursor requires a gap buffer");
        pith_value_free(gb_val);
        return false;
    }

    size_t pos = pith_gapbuf_cursor(gb_val.as.gapbuf);
    pith_value_free(gb_val);
    return pith_push(rt, PITH_NUMBER((double)pos));
}

static bool builtin_gap_length(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue gb_val = pith_pop(rt);

    if (!PITH_IS_GAPBUF(gb_val)) {
        pith_error(rt, "gap-length requires a gap buffer");
        pith_value_free(gb_val);
        return false;
    }

    size_t len = pith_gapbuf_length(gb_val.as.gapbuf);
    pith_value_free(gb_val);
    return pith_push(rt, PITH_NUMBER((double)len));
}

static bool builtin_gap_char(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue gb_val = pith_pop(rt);
    PithValue pos_val = pith_pop(rt);

    if (!PITH_IS_GAPBUF(gb_val)) {
        pith_error(rt, "gap-char requires a gap buffer");
        pith_value_free(gb_val);
        pith_value_free(pos_val);
        return false;
    }
    if (!PITH_IS_NUMBER(pos_val)) {
        pith_error(rt, "gap-char requires a position");
        pith_value_free(gb_val);
        pith_value_free(pos_val);
        return false;
    }

    char c = pith_gapbuf_char_at(gb_val.as.gapbuf, (size_t)pos_val.as.number);
    pith_value_free(gb_val);

    if (c == '\0') {
        return pith_push(rt, PITH_NIL());
    }

    char *str = malloc(2);
    str[0] = c;
    str[1] = '\0';
    return pith_push(rt, PITH_STRING(str));
}

/* ========================================================================
   FILE SYSTEM OPERATIONS
   ======================================================================== */

/* file-read: ( path -- contents ) */
static bool builtin_file_read(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue path = pith_pop(rt);

    if (!PITH_IS_STRING(path)) {
        pith_error(rt, "file-read requires a string path");
        pith_value_free(path);
        return false;
    }

    FILE *f = fopen(path.as.string, "rb");
    if (!f) {
        pith_value_free(path);
        return pith_push(rt, PITH_NIL());
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *contents = malloc(size + 1);
    if (!contents) {
        fclose(f);
        pith_value_free(path);
        pith_error(rt, "file-read: out of memory");
        return false;
    }

    size_t read = fread(contents, 1, size, f);
    contents[read] = '\0';
    fclose(f);
    pith_value_free(path);

    return pith_push(rt, PITH_STRING(contents));
}

/* file-write: ( contents path -- ) */
static bool builtin_file_write(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue path = pith_pop(rt);
    PithValue contents = pith_pop(rt);

    if (!PITH_IS_STRING(path)) {
        pith_error(rt, "file-write requires a string path");
        pith_value_free(path);
        pith_value_free(contents);
        return false;
    }
    if (!PITH_IS_STRING(contents)) {
        pith_error(rt, "file-write requires string contents");
        pith_value_free(path);
        pith_value_free(contents);
        return false;
    }

    FILE *f = fopen(path.as.string, "wb");
    if (!f) {
        pith_error(rt, "file-write: could not open file for writing");
        pith_value_free(path);
        pith_value_free(contents);
        return false;
    }

    size_t len = strlen(contents.as.string);
    fwrite(contents.as.string, 1, len, f);
    fclose(f);

    pith_value_free(path);
    pith_value_free(contents);
    return true;
}

/* file-exists: ( path -- bool ) */
static bool builtin_file_exists(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue path = pith_pop(rt);

    if (!PITH_IS_STRING(path)) {
        pith_error(rt, "file-exists requires a string path");
        pith_value_free(path);
        return false;
    }

    struct stat st;
    bool exists = (stat(path.as.string, &st) == 0);
    pith_value_free(path);

    return pith_push(rt, PITH_BOOL(exists));
}

/* dir-list: ( path -- array ) */
static bool builtin_dir_list(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue path = pith_pop(rt);

    if (!PITH_IS_STRING(path)) {
        pith_error(rt, "dir-list requires a string path");
        pith_value_free(path);
        return false;
    }

    DIR *dir = opendir(path.as.string);
    if (!dir) {
        pith_value_free(path);
        return pith_push(rt, PITH_NIL());
    }

    PithArray *arr = pith_array_new();
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        pith_array_push(arr, PITH_STRING(pith_strdup(entry->d_name)));
    }

    closedir(dir);
    pith_value_free(path);

    return pith_push(rt, PITH_ARRAY(arr));
}

/* file-append: ( contents path -- ) */
static bool builtin_file_append(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue path = pith_pop(rt);
    PithValue contents = pith_pop(rt);

    if (!PITH_IS_STRING(path)) {
        pith_error(rt, "file-append requires a string path");
        pith_value_free(path);
        pith_value_free(contents);
        return false;
    }
    if (!PITH_IS_STRING(contents)) {
        pith_error(rt, "file-append requires string contents");
        pith_value_free(path);
        pith_value_free(contents);
        return false;
    }

    FILE *f = fopen(path.as.string, "ab");
    if (!f) {
        pith_error(rt, "file-append: could not open file for appending");
        pith_value_free(path);
        pith_value_free(contents);
        return false;
    }

    size_t len = strlen(contents.as.string);
    fwrite(contents.as.string, 1, len, f);
    fclose(f);

    pith_value_free(path);
    pith_value_free(contents);
    return true;
}

/* ========================================================================
   PATH-BASED ACCESS
   ======================================================================== */

/* set-path: ( value path -- ) sets value at dot-separated path */
static bool builtin_set_path(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue path = pith_pop(rt);
    PithValue value = pith_pop(rt);

    if (!PITH_IS_STRING(path)) {
        pith_error(rt, "set-path requires a string path");
        pith_value_free(path);
        pith_value_free(value);
        return false;
    }

    /* Parse path "a.b.c" into parts */
    char *path_copy = pith_strdup(path.as.string);
    char *parts[64];
    int part_count = 0;

    char *token = strtok(path_copy, ".");
    while (token && part_count < 64) {
        parts[part_count++] = token;
        token = strtok(NULL, ".");
    }

    if (part_count == 0) {
        pith_error(rt, "set-path: empty path");
        free(path_copy);
        pith_value_free(path);
        pith_value_free(value);
        return false;
    }

    /* Find the root dictionary */
    PithDict *current = pith_find_dict(rt, parts[0]);
    if (!current) {
        pith_error(rt, "set-path: unknown dictionary '%s'", parts[0]);
        free(path_copy);
        pith_value_free(path);
        pith_value_free(value);
        return false;
    }

    /* Traverse to the parent of the final slot */
    for (int i = 1; i < part_count - 1; i++) {
        PithSlot *slot = pith_dict_lookup(current, parts[i]);
        if (!slot) {
            pith_error(rt, "set-path: unknown slot '%s'", parts[i]);
            free(path_copy);
            pith_value_free(path);
            pith_value_free(value);
            return false;
        }

        if (slot->is_cached && slot->cached.type == VAL_DICT) {
            current = slot->cached.as.dict;
        } else if (!slot->is_cached) {
            /* Execute slot to get value */
            PithDict *saved_dict = rt->current_dict;
            rt->current_dict = current;
            if (!pith_execute_slot(rt, slot)) {
                rt->current_dict = saved_dict;
                free(path_copy);
                pith_value_free(path);
                pith_value_free(value);
                return false;
            }
            rt->current_dict = saved_dict;

            PithValue val = pith_pop(rt);
            if (val.type != VAL_DICT) {
                pith_error(rt, "set-path: '%s' is not a dictionary", parts[i]);
                pith_value_free(val);
                free(path_copy);
                pith_value_free(path);
                pith_value_free(value);
                return false;
            }
            current = val.as.dict;
            /* Don't free val - we're using its dict pointer */
        } else {
            pith_error(rt, "set-path: '%s' is not a dictionary", parts[i]);
            free(path_copy);
            pith_value_free(path);
            pith_value_free(value);
            return false;
        }
    }

    /* Set the final slot */
    const char *final_name = parts[part_count - 1];
    pith_dict_set_value(current, final_name, value);

    free(path_copy);
    pith_value_free(path);
    return true;
}

/* get-path: ( path -- value ) gets value at dot-separated path */
static bool builtin_get_path(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue path = pith_pop(rt);

    if (!PITH_IS_STRING(path)) {
        pith_error(rt, "get-path requires a string path");
        pith_value_free(path);
        return false;
    }

    /* Parse path "a.b.c" into parts */
    char *path_copy = pith_strdup(path.as.string);
    char *parts[64];
    int part_count = 0;

    char *token = strtok(path_copy, ".");
    while (token && part_count < 64) {
        parts[part_count++] = token;
        token = strtok(NULL, ".");
    }

    if (part_count == 0) {
        pith_error(rt, "get-path: empty path");
        free(path_copy);
        pith_value_free(path);
        return false;
    }

    /* Find the root dictionary */
    PithDict *current = pith_find_dict(rt, parts[0]);
    if (!current) {
        pith_error(rt, "get-path: unknown dictionary '%s'", parts[0]);
        free(path_copy);
        pith_value_free(path);
        return false;
    }

    /* Traverse to the final slot */
    for (int i = 1; i < part_count; i++) {
        PithSlot *slot = pith_dict_lookup(current, parts[i]);
        if (!slot) {
            pith_error(rt, "get-path: unknown slot '%s'", parts[i]);
            free(path_copy);
            pith_value_free(path);
            return false;
        }

        if (i == part_count - 1) {
            /* Final slot - return its value */
            if (slot->is_cached) {
                free(path_copy);
                pith_value_free(path);
                return pith_push(rt, pith_value_copy(slot->cached));
            } else {
                /* Execute slot to get value */
                PithDict *saved_dict = rt->current_dict;
                rt->current_dict = current;
                bool result = pith_execute_slot(rt, slot);
                rt->current_dict = saved_dict;
                free(path_copy);
                pith_value_free(path);
                return result;
            }
        } else {
            /* Intermediate slot - must be a dictionary */
            if (slot->is_cached && slot->cached.type == VAL_DICT) {
                current = slot->cached.as.dict;
            } else {
                pith_error(rt, "get-path: '%s' is not a dictionary", parts[i]);
                free(path_copy);
                pith_value_free(path);
                return false;
            }
        }
    }

    /* Should not reach here, but handle single-part path */
    free(path_copy);
    pith_value_free(path);
    pith_error(rt, "get-path: invalid path");
    return false;
}

/* Printing */
static bool builtin_print(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    char *str = pith_value_to_string(a);
    printf("%s\n", str);
    free(str);
    pith_value_free(a);
    return true;
}

/* UI primitives */
static bool builtin_text(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    if (!PITH_IS_STRING(a)) {
        pith_error(rt, "text requires string");
        return false;
    }
    PithView *view = pith_view_text(a.as.string);
    pith_value_free(a);
    return pith_push(rt, PITH_VIEW(view));
}

static bool builtin_textfield(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);

    PithView *view = NULL;
    if (PITH_IS_STRING(a)) {
        /* Create textfield from string */
        view = pith_view_textfield(a.as.string, NULL);
    } else if (PITH_IS_GAPBUF(a)) {
        /* Create textfield with existing gap buffer */
        view = malloc(sizeof(PithView));
        memset(view, 0, sizeof(PithView));
        view->type = VIEW_TEXTFIELD;
        view->as.textfield.buffer = pith_gapbuf_copy(a.as.gapbuf);
        view->as.textfield.on_change = NULL;
    } else {
        pith_error(rt, "textfield requires string or gapbuf");
        return false;
    }

    pith_value_free(a);
    return pith_push(rt, PITH_VIEW(view));
}

static bool builtin_signal(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue initial = pith_pop(rt);
    PithSignal *sig = pith_signal_new(rt, initial);
    return pith_push(rt, PITH_SIGNAL(sig));
}

static bool builtin_button(PithRuntime *rt) {
    /* ( label block -- view ) or ( label -- view ) */
    if (!pith_stack_has(rt, 1)) return false;

    PithBlock *on_click = NULL;
    PithValue top = pith_peek(rt);

    /* Check if top of stack is a block (on-click handler) */
    if (PITH_IS_BLOCK(top)) {
        on_click = malloc(sizeof(PithBlock));
        *on_click = *pith_pop(rt).as.block;
        if (!pith_stack_has(rt, 1)) {
            free(on_click);
            pith_error(rt, "button requires label");
            return false;
        }
    }

    PithValue label_val = pith_pop(rt);
    if (!PITH_IS_STRING(label_val)) {
        free(on_click);
        pith_error(rt, "button requires string label");
        return false;
    }

    PithView *view = pith_view_button(label_val.as.string, on_click);
    pith_value_free(label_val);
    return pith_push(rt, PITH_VIEW(view));
}

static bool builtin_vstack(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    if (!PITH_IS_ARRAY(a)) {
        pith_error(rt, "vstack requires array");
        return false;
    }
    
    PithArray *arr = a.as.array;
    PithView **children = malloc(arr->length * sizeof(PithView*));
    size_t count = 0;
    
    for (size_t i = 0; i < arr->length; i++) {
        if (PITH_IS_VIEW(arr->items[i])) {
            children[count++] = arr->items[i].as.view;
        }
    }
    
    PithView *view = pith_view_vstack(children, count);
    free(children);
    free(arr->items);
    free(arr);
    
    return pith_push(rt, PITH_VIEW(view));
}

static bool builtin_hstack(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue a = pith_pop(rt);
    if (!PITH_IS_ARRAY(a)) {
        pith_error(rt, "hstack requires array");
        return false;
    }
    
    PithArray *arr = a.as.array;
    PithView **children = malloc(arr->length * sizeof(PithView*));
    size_t count = 0;
    
    for (size_t i = 0; i < arr->length; i++) {
        if (PITH_IS_VIEW(arr->items[i])) {
            children[count++] = arr->items[i].as.view;
        }
    }
    
    PithView *view = pith_view_hstack(children, count);
    free(children);
    free(arr->items);
    free(arr);

    return pith_push(rt, PITH_VIEW(view));
}

static bool builtin_spacer(PithRuntime *rt) {
    PithView *view = malloc(sizeof(PithView));
    memset(view, 0, sizeof(PithView));
    view->type = VIEW_SPACER;
    view->style.fill = true;
    return pith_push(rt, PITH_VIEW(view));
}

/* map: array block -> array */
/* Applies block to each element, collects results */
/* Array Operations */
static bool builtin_first(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue arr = pith_pop(rt);
    if (!PITH_IS_ARRAY(arr)) {
        pith_error(rt, "first requires an array");
        pith_value_free(arr);
        return false;
    }
    if (arr.as.array->length == 0) {
        pith_value_free(arr);
        return pith_push(rt, PITH_NIL());
    }
    PithValue result = pith_value_copy(arr.as.array->items[0]);
    pith_value_free(arr);
    return pith_push(rt, result);
}

static bool builtin_last(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue arr = pith_pop(rt);
    if (!PITH_IS_ARRAY(arr)) {
        pith_error(rt, "last requires an array");
        pith_value_free(arr);
        return false;
    }
    if (arr.as.array->length == 0) {
        pith_value_free(arr);
        return pith_push(rt, PITH_NIL());
    }
    PithValue result = pith_value_copy(arr.as.array->items[arr.as.array->length - 1]);
    pith_value_free(arr);
    return pith_push(rt, result);
}

static bool builtin_nth(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue idx = pith_pop(rt);
    PithValue arr = pith_pop(rt);
    if (!PITH_IS_ARRAY(arr) || !PITH_IS_NUMBER(idx)) {
        pith_error(rt, "nth requires array and index");
        pith_value_free(arr);
        pith_value_free(idx);
        return false;
    }
    int n = (int)idx.as.number;
    if (n < 0 || (size_t)n >= arr.as.array->length) {
        pith_value_free(arr);
        return pith_push(rt, PITH_NIL());
    }
    PithValue result = pith_value_copy(arr.as.array->items[n]);
    pith_value_free(arr);
    return pith_push(rt, result);
}

static bool builtin_append(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue item = pith_pop(rt);
    PithValue arr = pith_pop(rt);
    if (!PITH_IS_ARRAY(arr)) {
        pith_error(rt, "append requires an array");
        pith_value_free(arr);
        pith_value_free(item);
        return false;
    }
    PithArray *new_arr = pith_array_new();
    for (size_t i = 0; i < arr.as.array->length; i++) {
        pith_array_push(new_arr, pith_value_copy(arr.as.array->items[i]));
    }
    pith_array_push(new_arr, item);
    pith_value_free(arr);
    return pith_push(rt, PITH_ARRAY(new_arr));
}

static bool builtin_prepend(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue arr = pith_pop(rt);
    PithValue item = pith_pop(rt);
    if (!PITH_IS_ARRAY(arr)) {
        pith_error(rt, "prepend requires an array");
        pith_value_free(arr);
        pith_value_free(item);
        return false;
    }
    PithArray *new_arr = pith_array_new();
    pith_array_push(new_arr, item);
    for (size_t i = 0; i < arr.as.array->length; i++) {
        pith_array_push(new_arr, pith_value_copy(arr.as.array->items[i]));
    }
    pith_value_free(arr);
    return pith_push(rt, PITH_ARRAY(new_arr));
}

static bool builtin_slice(PithRuntime *rt) {
    if (!pith_stack_has(rt, 3)) return false;
    PithValue end_val = pith_pop(rt);
    PithValue start_val = pith_pop(rt);
    PithValue arr = pith_pop(rt);
    if (!PITH_IS_ARRAY(arr) || !PITH_IS_NUMBER(start_val) || !PITH_IS_NUMBER(end_val)) {
        pith_error(rt, "slice requires array, start, end");
        pith_value_free(arr);
        pith_value_free(start_val);
        pith_value_free(end_val);
        return false;
    }
    int start = (int)start_val.as.number;
    int end = (int)end_val.as.number;
    size_t len = arr.as.array->length;

    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if ((size_t)start > len) start = len;
    if ((size_t)end > len) end = len;
    if (start > end) start = end;

    PithArray *new_arr = pith_array_new();
    for (int i = start; i < end; i++) {
        pith_array_push(new_arr, pith_value_copy(arr.as.array->items[i]));
    }
    pith_value_free(arr);
    return pith_push(rt, PITH_ARRAY(new_arr));
}

static bool builtin_reverse(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue arr = pith_pop(rt);
    if (!PITH_IS_ARRAY(arr)) {
        pith_error(rt, "reverse requires an array");
        pith_value_free(arr);
        return false;
    }
    PithArray *new_arr = pith_array_new();
    for (size_t i = arr.as.array->length; i > 0; i--) {
        pith_array_push(new_arr, pith_value_copy(arr.as.array->items[i - 1]));
    }
    pith_value_free(arr);
    return pith_push(rt, PITH_ARRAY(new_arr));
}

static int pith_value_compare(const void *a, const void *b) {
    const PithValue *va = (const PithValue *)a;
    const PithValue *vb = (const PithValue *)b;

    if (PITH_IS_NUMBER(*va) && PITH_IS_NUMBER(*vb)) {
        double diff = va->as.number - vb->as.number;
        return (diff > 0) - (diff < 0);
    }
    if (PITH_IS_STRING(*va) && PITH_IS_STRING(*vb)) {
        return strcmp(va->as.string, vb->as.string);
    }
    return 0;
}

static bool builtin_sort(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue arr = pith_pop(rt);
    if (!PITH_IS_ARRAY(arr)) {
        pith_error(rt, "sort requires an array");
        pith_value_free(arr);
        return false;
    }
    PithArray *new_arr = pith_array_new();
    for (size_t i = 0; i < arr.as.array->length; i++) {
        pith_array_push(new_arr, pith_value_copy(arr.as.array->items[i]));
    }
    qsort(new_arr->items, new_arr->length, sizeof(PithValue), pith_value_compare);
    pith_value_free(arr);
    return pith_push(rt, PITH_ARRAY(new_arr));
}

static bool builtin_index_of(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue item = pith_pop(rt);
    PithValue arr = pith_pop(rt);
    if (!PITH_IS_ARRAY(arr)) {
        pith_error(rt, "index-of requires an array");
        pith_value_free(arr);
        pith_value_free(item);
        return false;
    }
    double idx = -1;
    for (size_t i = 0; i < arr.as.array->length; i++) {
        if (pith_value_equal(arr.as.array->items[i], item)) {
            idx = (double)i;
            break;
        }
    }
    pith_value_free(arr);
    pith_value_free(item);
    return pith_push(rt, PITH_NUMBER(idx));
}

static bool builtin_empty(PithRuntime *rt) {
    if (!pith_stack_has(rt, 1)) return false;
    PithValue arr = pith_pop(rt);
    if (!PITH_IS_ARRAY(arr)) {
        pith_error(rt, "empty? requires an array");
        pith_value_free(arr);
        return false;
    }
    bool empty = arr.as.array->length == 0;
    pith_value_free(arr);
    return pith_push(rt, PITH_BOOL(empty));
}

static bool builtin_filter(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue block_val = pith_pop(rt);
    PithValue arr_val = pith_pop(rt);

    if (!PITH_IS_ARRAY(arr_val)) {
        pith_error(rt, "filter requires array as first argument");
        pith_value_free(arr_val);
        pith_value_free(block_val);
        return false;
    }
    if (!PITH_IS_BLOCK(block_val)) {
        pith_error(rt, "filter requires block as second argument");
        pith_value_free(arr_val);
        pith_value_free(block_val);
        return false;
    }

    PithArray *input = arr_val.as.array;
    PithBlock *block = block_val.as.block;
    PithArray *output = pith_array_new();

    for (size_t i = 0; i < input->length; i++) {
        pith_push(rt, pith_value_copy(input->items[i]));
        pith_execute_block(rt, block);
        if (pith_stack_has(rt, 1)) {
            PithValue result = pith_pop(rt);
            bool keep = false;
            if (PITH_IS_BOOL(result)) keep = result.as.boolean;
            else if (PITH_IS_NUMBER(result)) keep = result.as.number != 0;
            else if (!PITH_IS_NIL(result)) keep = true;
            pith_value_free(result);
            if (keep) {
                pith_array_push(output, pith_value_copy(input->items[i]));
            }
        }
    }

    pith_value_free(arr_val);
    free(block);
    return pith_push(rt, PITH_ARRAY(output));
}

static bool builtin_each(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue block_val = pith_pop(rt);
    PithValue arr_val = pith_pop(rt);

    if (!PITH_IS_ARRAY(arr_val)) {
        pith_error(rt, "each requires array as first argument");
        pith_value_free(arr_val);
        pith_value_free(block_val);
        return false;
    }
    if (!PITH_IS_BLOCK(block_val)) {
        pith_error(rt, "each requires block as second argument");
        pith_value_free(arr_val);
        pith_value_free(block_val);
        return false;
    }

    PithArray *input = arr_val.as.array;
    PithBlock *block = block_val.as.block;

    for (size_t i = 0; i < input->length; i++) {
        pith_push(rt, pith_value_copy(input->items[i]));
        pith_execute_block(rt, block);
    }

    pith_value_free(arr_val);
    free(block);
    return true;
}

static bool builtin_reduce(PithRuntime *rt) {
    if (!pith_stack_has(rt, 3)) return false;
    PithValue block_val = pith_pop(rt);
    PithValue initial = pith_pop(rt);
    PithValue arr_val = pith_pop(rt);

    if (!PITH_IS_ARRAY(arr_val)) {
        pith_error(rt, "reduce requires array as first argument");
        pith_value_free(arr_val);
        pith_value_free(initial);
        pith_value_free(block_val);
        return false;
    }
    if (!PITH_IS_BLOCK(block_val)) {
        pith_error(rt, "reduce requires block as third argument");
        pith_value_free(arr_val);
        pith_value_free(initial);
        pith_value_free(block_val);
        return false;
    }

    PithArray *input = arr_val.as.array;
    PithBlock *block = block_val.as.block;
    PithValue accumulator = initial;

    for (size_t i = 0; i < input->length; i++) {
        pith_push(rt, accumulator);
        pith_push(rt, pith_value_copy(input->items[i]));
        pith_execute_block(rt, block);
        if (pith_stack_has(rt, 1)) {
            accumulator = pith_pop(rt);
        } else {
            accumulator = PITH_NIL();
        }
    }

    pith_value_free(arr_val);
    free(block);
    return pith_push(rt, accumulator);
}

static bool builtin_find(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue block_val = pith_pop(rt);
    PithValue arr_val = pith_pop(rt);

    if (!PITH_IS_ARRAY(arr_val)) {
        pith_error(rt, "find requires array as first argument");
        pith_value_free(arr_val);
        pith_value_free(block_val);
        return false;
    }
    if (!PITH_IS_BLOCK(block_val)) {
        pith_error(rt, "find requires block as second argument");
        pith_value_free(arr_val);
        pith_value_free(block_val);
        return false;
    }

    PithArray *input = arr_val.as.array;
    PithBlock *block = block_val.as.block;
    PithValue found = PITH_NIL();

    for (size_t i = 0; i < input->length; i++) {
        pith_push(rt, pith_value_copy(input->items[i]));
        pith_execute_block(rt, block);
        if (pith_stack_has(rt, 1)) {
            PithValue result = pith_pop(rt);
            bool match = false;
            if (PITH_IS_BOOL(result)) match = result.as.boolean;
            else if (PITH_IS_NUMBER(result)) match = result.as.number != 0;
            else if (!PITH_IS_NIL(result)) match = true;
            pith_value_free(result);
            if (match) {
                found = pith_value_copy(input->items[i]);
                break;
            }
        }
    }

    pith_value_free(arr_val);
    free(block);
    return pith_push(rt, found);
}

static bool builtin_any(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue block_val = pith_pop(rt);
    PithValue arr_val = pith_pop(rt);

    if (!PITH_IS_ARRAY(arr_val)) {
        pith_error(rt, "any requires array as first argument");
        pith_value_free(arr_val);
        pith_value_free(block_val);
        return false;
    }
    if (!PITH_IS_BLOCK(block_val)) {
        pith_error(rt, "any requires block as second argument");
        pith_value_free(arr_val);
        pith_value_free(block_val);
        return false;
    }

    PithArray *input = arr_val.as.array;
    PithBlock *block = block_val.as.block;
    bool any_match = false;

    for (size_t i = 0; i < input->length; i++) {
        pith_push(rt, pith_value_copy(input->items[i]));
        pith_execute_block(rt, block);
        if (pith_stack_has(rt, 1)) {
            PithValue result = pith_pop(rt);
            bool match = false;
            if (PITH_IS_BOOL(result)) match = result.as.boolean;
            else if (PITH_IS_NUMBER(result)) match = result.as.number != 0;
            else if (!PITH_IS_NIL(result)) match = true;
            pith_value_free(result);
            if (match) {
                any_match = true;
                break;
            }
        }
    }

    pith_value_free(arr_val);
    free(block);
    return pith_push(rt, PITH_BOOL(any_match));
}

static bool builtin_all(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue block_val = pith_pop(rt);
    PithValue arr_val = pith_pop(rt);

    if (!PITH_IS_ARRAY(arr_val)) {
        pith_error(rt, "all requires array as first argument");
        pith_value_free(arr_val);
        pith_value_free(block_val);
        return false;
    }
    if (!PITH_IS_BLOCK(block_val)) {
        pith_error(rt, "all requires block as second argument");
        pith_value_free(arr_val);
        pith_value_free(block_val);
        return false;
    }

    PithArray *input = arr_val.as.array;
    PithBlock *block = block_val.as.block;
    bool all_match = true;

    for (size_t i = 0; i < input->length; i++) {
        pith_push(rt, pith_value_copy(input->items[i]));
        pith_execute_block(rt, block);
        if (pith_stack_has(rt, 1)) {
            PithValue result = pith_pop(rt);
            bool match = false;
            if (PITH_IS_BOOL(result)) match = result.as.boolean;
            else if (PITH_IS_NUMBER(result)) match = result.as.number != 0;
            else if (!PITH_IS_NIL(result)) match = true;
            pith_value_free(result);
            if (!match) {
                all_match = false;
                break;
            }
        }
    }

    pith_value_free(arr_val);
    free(block);
    return pith_push(rt, PITH_BOOL(all_match));
}

static bool builtin_map(PithRuntime *rt) {
    if (!pith_stack_has(rt, 2)) return false;
    PithValue block_val = pith_pop(rt);
    PithValue arr_val = pith_pop(rt);

    if (!PITH_IS_ARRAY(arr_val)) {
        pith_error(rt, "map requires array as first argument");
        return false;
    }
    if (!PITH_IS_BLOCK(block_val)) {
        pith_error(rt, "map requires block as second argument");
        return false;
    }

    PithArray *input = arr_val.as.array;
    PithBlock *block = block_val.as.block;
    PithArray *output = pith_array_new();

    for (size_t i = 0; i < input->length; i++) {
        /* Push current element onto stack */
        pith_push(rt, pith_value_copy(input->items[i]));

        /* Execute the block */
        pith_execute_block(rt, block);

        /* Collect result */
        if (pith_stack_has(rt, 1)) {
            pith_array_push(output, pith_pop(rt));
        }
    }

    pith_value_free(arr_val);
    free(block);

    return pith_push(rt, PITH_ARRAY(output));
}

/* ========================================================================
   BUILTIN REGISTRATION
   ======================================================================== */

typedef struct {
    const char *name;
    PithBuiltinFn fn;
} BuiltinEntry;

static BuiltinEntry builtins[] = {
    /* Stack */
    {"dup", builtin_dup},
    {"drop", builtin_drop},
    {"swap", builtin_swap},
    {"over", builtin_over},
    {"rot", builtin_rot},

    /* Arithmetic */
    {"add", builtin_add},
    {"+", builtin_add},
    {"subtract", builtin_subtract},
    {"-", builtin_subtract},
    {"multiply", builtin_multiply},
    {"*", builtin_multiply},
    {"divide", builtin_divide},
    {"/", builtin_divide},
    {"mod", builtin_mod},
    {"abs", builtin_abs},
    {"min", builtin_min},
    {"max", builtin_max},

    /* Comparison */
    {"=", builtin_equal},
    {"<", builtin_less},
    {">", builtin_greater},
    {"!=", builtin_not_equal},
    {"<=", builtin_less_equal},
    {">=", builtin_greater_equal},

    /* Logic */
    {"and", builtin_and},
    {"or", builtin_or},
    {"not", builtin_not},
    
    /* Strings/Arrays */
    {"length", builtin_length},
    {"concat", builtin_concat},
    {"split", builtin_split},
    {"join", builtin_join},
    {"trim", builtin_trim},
    {"substring", builtin_substring},
    {"contains", builtin_contains},
    {"replace", builtin_replace},
    {"uppercase", builtin_uppercase},
    {"lowercase", builtin_lowercase},
    {"lines", builtin_lines},
    {"words", builtin_words},

    /* Debug */
    {"print", builtin_print},
    
    /* UI */
    {"text", builtin_text},
    {"textfield", builtin_textfield},
    {"vstack", builtin_vstack},

    /* Signals */
    {"signal", builtin_signal},
    {"button", builtin_button},
    {"hstack", builtin_hstack},
    {"spacer", builtin_spacer},

    /* Arrays */
    {"first", builtin_first},
    {"last", builtin_last},
    {"nth", builtin_nth},
    {"append", builtin_append},
    {"prepend", builtin_prepend},
    {"slice", builtin_slice},
    {"reverse", builtin_reverse},
    {"sort", builtin_sort},
    {"index-of", builtin_index_of},
    {"empty?", builtin_empty},

    /* Functional */
    {"map", builtin_map},
    {"filter", builtin_filter},
    {"each", builtin_each},
    {"reduce", builtin_reduce},
    {"find", builtin_find},
    {"any", builtin_any},
    {"all", builtin_all},

    /* Type Checking */
    {"type", builtin_type},
    {"string?", builtin_is_string},
    {"number?", builtin_is_number},
    {"array?", builtin_is_array},
    {"map?", builtin_is_map},
    {"bool?", builtin_is_bool},
    {"nil?", builtin_is_nil},

    /* Type Conversion */
    {"to-string", builtin_to_string},
    {"to-number", builtin_to_number},

    /* Map Operations */
    {"new-map", builtin_map_new},
    {"get", builtin_map_get},
    {"set", builtin_map_set},
    {"keys", builtin_map_keys},
    {"values", builtin_map_values},
    {"has", builtin_map_has},
    {"remove", builtin_map_remove},
    {"merge", builtin_map_merge},
    {"sanitize", builtin_sanitize},
    {"to-json", builtin_to_json},
    {"parse-json", builtin_parse_json},

    /* Gap Buffer Operations */
    {"new-gap", builtin_gap_new},
    {"string-to-gap", builtin_string_to_gap},
    {"gap-to-string", builtin_gap_to_string},
    {"gap-insert", builtin_gap_insert},
    {"gap-delete", builtin_gap_delete},
    {"gap-move", builtin_gap_move},
    {"gap-goto", builtin_gap_goto},
    {"gap-cursor", builtin_gap_cursor},
    {"gap-length", builtin_gap_length},
    {"gap-char", builtin_gap_char},

    /* File system */
    {"file-read", builtin_file_read},
    {"file-write", builtin_file_write},
    {"file-exists", builtin_file_exists},
    {"dir-list", builtin_dir_list},
    {"file-append", builtin_file_append},

    /* Path-based access */
    {"set-path", builtin_set_path},
    {"get-path", builtin_get_path},

    {NULL, NULL}
};

static void register_builtins(PithRuntime *rt) {
    for (int i = 0; builtins[i].name != NULL; i++) {
        /* Store builtin function pointer in a special way */
        /* For now, we'll handle builtins specially in execute */
    }
}

/* ========================================================================
   EXECUTION
   ======================================================================== */

static PithBuiltinFn find_builtin(const char *name) {
    for (int i = 0; builtins[i].name != NULL; i++) {
        if (strcmp(builtins[i].name, name) == 0) {
            return builtins[i].fn;
        }
    }
    return NULL;
}

/* Forward declaration */
static PithDict* pith_find_dict(PithRuntime *rt, const char *name);

static int g_exec_depth = 0;

bool pith_execute_word(PithRuntime *rt, const char *name) {
    if (g_debug && g_exec_depth < 20) {
        fprintf(stderr, "[DEBUG] %*sexec word: %s (dict=%s, stack=%zu)\n",
                g_exec_depth * 2, "", name,
                rt->current_dict ? rt->current_dict->name : "(null)",
                rt->stack_top);
    }
    g_exec_depth++;

    /* Check for signal write syntax: word! */
    size_t name_len = strlen(name);
    if (name_len > 1 && name[name_len - 1] == '!') {
        /* Get the slot name without the ! */
        char *slot_name = malloc(name_len);
        memcpy(slot_name, name, name_len - 1);
        slot_name[name_len - 1] = '\0';

        /* Look up the slot */
        PithSlot *slot = NULL;
        if (rt->current_dict) {
            slot = pith_dict_lookup(rt->current_dict, slot_name);
        }
        if (!slot) {
            /* Try root dict */
            slot = pith_dict_lookup(rt->root, slot_name);
        }

        if (slot && slot->is_cached && slot->cached.type == VAL_SIGNAL) {
            /* Pop value from stack and set signal */
            if (!pith_stack_has(rt, 1)) {
                free(slot_name);
                g_exec_depth--;
                pith_error(rt, "Signal write requires value on stack");
                return false;
            }
            PithValue new_val = pith_pop(rt);
            pith_signal_set(slot->cached.as.signal, new_val);
            free(slot_name);
            g_exec_depth--;
            return true;
        }

        free(slot_name);
        g_exec_depth--;
        pith_error(rt, "Unknown signal: %s", name);
        return false;
    }

    /* Check builtins first */
    PithBuiltinFn builtin = find_builtin(name);
    if (builtin) {
        bool result = builtin(rt);
        g_exec_depth--;
        return result;
    }

    /* Look up in current dictionary */
    if (rt->current_dict) {
        PithSlot *slot = pith_dict_lookup(rt->current_dict, name);
        if (slot) {
            /* If slot is a cached dictionary, execute its ui slot or push it */
            if (slot->is_cached && slot->cached.type == VAL_DICT) {
                PithDict *dict = slot->cached.as.dict;
                PithSlot *ui_slot = pith_dict_lookup(dict, "ui");
                if (ui_slot) {
                    PithDict *saved_dict = rt->current_dict;
                    rt->current_dict = dict;
                    bool result = pith_execute_slot(rt, ui_slot);
                    rt->current_dict = saved_dict;
                    /* Apply dictionary styles to the view if one was produced */
                    if (result && rt->stack_top > 0) {
                        PithValue *top = &rt->stack[rt->stack_top - 1];
                        if (top->type == VAL_VIEW) {
                            pith_apply_dict_styles(dict, top->as.view);
                        }
                    }
                    g_exec_depth--;
                    return result;
                } else {
                    /* No ui slot - push dictionary as value */
                    g_exec_depth--;
                    return pith_push(rt, PITH_DICT(dict));
                }
            } else {
                bool result = pith_execute_slot(rt, slot);
                g_exec_depth--;
                return result;
            }
        }
    }

    /* Check if it's a dictionary name in root */
    PithDict *dict = pith_find_dict(rt, name);
    if (dict) {
        PithSlot *ui_slot = pith_dict_lookup(dict, "ui");
        if (ui_slot) {
            /* Has ui slot - execute it */
            PithDict *saved_dict = rt->current_dict;
            rt->current_dict = dict;
            bool result = pith_execute_slot(rt, ui_slot);
            rt->current_dict = saved_dict;
            /* Apply dictionary styles to the view if one was produced */
            if (result && rt->stack_top > 0) {
                PithValue *top = &rt->stack[rt->stack_top - 1];
                if (top->type == VAL_VIEW) {
                    pith_apply_dict_styles(dict, top->as.view);
                }
            }
            g_exec_depth--;
            return result;
        } else {
            /* No ui slot - push dictionary as value */
            g_exec_depth--;
            return pith_push(rt, PITH_DICT(dict));
        }
    }

    g_exec_depth--;
    pith_error(rt, "Unknown word: %s", name);
    return false;
}

bool pith_execute_slot(PithRuntime *rt, PithSlot *slot) {
    /* If slot has a cached value, push it instead of executing body */
    if (slot->is_cached) {
        /* Always auto-unwrap signals when reading */
        if (slot->cached.type == VAL_SIGNAL) {
            PithSignal *sig = slot->cached.as.signal;
            return pith_push(rt, pith_value_copy(sig->value));
        }
        return pith_push(rt, pith_value_copy(slot->cached));
    }

    /* Execute tokens from body_start to body_end */
    for (size_t i = slot->body_start; i < slot->body_end; i++) {
        PithToken *tok = &rt->tokens[i];
        
        switch (tok->type) {
            case TOK_NUMBER:
                pith_push(rt, PITH_NUMBER(atof(tok->text)));
                break;
                
            case TOK_STRING:
                pith_push(rt, PITH_STRING(pith_strdup(tok->text)));
                break;
                
            case TOK_TRUE:
                pith_push(rt, PITH_BOOL(true));
                break;
                
            case TOK_FALSE:
                pith_push(rt, PITH_BOOL(false));
                break;
                
            case TOK_NIL:
                pith_push(rt, PITH_NIL());
                break;
                
            case TOK_WORD:
                /* Check for dot-access: word.slot or word.slot.nested... */
                if (i + 2 < slot->body_end &&
                    rt->tokens[i + 1].type == TOK_DOT &&
                    rt->tokens[i + 2].type == TOK_WORD) {

                    /* Collect all parts of the dot chain */
                    size_t chain_start = i;
                    size_t chain_end = i;
                    while (chain_end + 2 < slot->body_end &&
                           rt->tokens[chain_end + 1].type == TOK_DOT &&
                           rt->tokens[chain_end + 2].type == TOK_WORD) {
                        chain_end += 2;
                    }

                    /* First part is a dictionary name */
                    const char *dict_name = rt->tokens[chain_start].text;
                    PithDict *current = pith_find_dict(rt, dict_name);
                    if (!current) {
                        pith_error(rt, "Unknown dictionary: %s", dict_name);
                        return false;
                    }

                    /* Traverse intermediate parts (all but the last) */
                    for (size_t j = chain_start + 2; j < chain_end; j += 2) {
                        const char *part_name = rt->tokens[j].text;
                        PithSlot *part_slot = pith_dict_lookup(current, part_name);
                        if (!part_slot) {
                            pith_error(rt, "Unknown slot '%s' in path", part_name);
                            return false;
                        }

                        /* Get the value - must be a dict/map */
                        if (part_slot->is_cached && part_slot->cached.type == VAL_DICT) {
                            current = part_slot->cached.as.dict;
                        } else {
                            /* Execute slot to get value */
                            PithDict *saved_dict = rt->current_dict;
                            rt->current_dict = current;
                            if (!pith_execute_slot(rt, part_slot)) {
                                rt->current_dict = saved_dict;
                                return false;
                            }
                            rt->current_dict = saved_dict;

                            PithValue val = pith_pop(rt);
                            if (val.type != VAL_DICT) {
                                pith_error(rt, "'%s' is not a dictionary/map", part_name);
                                pith_value_free(val);
                                return false;
                            }
                            current = val.as.dict;
                            /* Note: we don't free val here as we're using its dict */
                        }
                    }

                    /* Get the final slot name */
                    const char *final_name = rt->tokens[chain_end].text;
                    size_t final_len = strlen(final_name);

                    /* Check if this is a signal write (ends with !) */
                    if (final_len > 1 && final_name[final_len - 1] == '!') {
                        /* Signal write: strip the ! and look up the signal */
                        char *slot_name = malloc(final_len);
                        memcpy(slot_name, final_name, final_len - 1);
                        slot_name[final_len - 1] = '\0';

                        PithSlot *final_slot = pith_dict_lookup(current, slot_name);
                        free(slot_name);

                        if (!final_slot || !final_slot->is_cached ||
                            final_slot->cached.type != VAL_SIGNAL) {
                            pith_error(rt, "Unknown signal '%s'", final_name);
                            return false;
                        }

                        /* Pop value from stack and set signal */
                        if (!pith_stack_has(rt, 1)) {
                            pith_error(rt, "Signal write requires value on stack");
                            return false;
                        }
                        PithValue new_val = pith_pop(rt);
                        pith_signal_set(final_slot->cached.as.signal, new_val);

                        /* Skip all tokens in the chain */
                        i = chain_end;
                    } else {
                        /* Normal slot execution */
                        PithSlot *final_slot = pith_dict_lookup(current, final_name);
                        if (!final_slot) {
                            pith_error(rt, "Unknown slot '%s' in path", final_name);
                            return false;
                        }

                        PithDict *saved_dict = rt->current_dict;
                        rt->current_dict = current;
                        if (!pith_execute_slot(rt, final_slot)) {
                            rt->current_dict = saved_dict;
                            return false;
                        }
                        rt->current_dict = saved_dict;

                        /* Skip all tokens in the chain */
                        i = chain_end;
                    }
                } else {
                    if (!pith_execute_word(rt, tok->text)) {
                        return false;
                    }
                }
                break;
                
            case TOK_IF: {
                /* If-else implementation */
                /* Syntax: condition if ... end  OR  condition if ... else ... end */
                if (!pith_stack_has(rt, 1)) {
                    pith_error(rt, "if requires condition on stack");
                    return false;
                }
                PithValue cond = pith_pop(rt);
                bool take_branch = false;
                if (cond.type == VAL_BOOL) {
                    take_branch = cond.as.boolean;
                } else if (cond.type == VAL_NUMBER) {
                    take_branch = cond.as.number != 0;
                } else if (cond.type == VAL_NIL) {
                    take_branch = false;
                } else {
                    /* Non-nil, non-false values are truthy */
                    take_branch = true;
                }

                /* Find matching else/end, tracking nesting depth */
                size_t if_body_start = i + 1;
                size_t else_pos = 0;  /* 0 means no else */
                size_t end_pos = if_body_start;
                int depth = 1;

                for (size_t j = if_body_start; j < slot->body_end; j++) {
                    PithTokenType t = rt->tokens[j].type;
                    if (t == TOK_IF || t == TOK_DO) {
                        depth++;
                    } else if (t == TOK_ELSE && depth == 1) {
                        else_pos = j;
                    } else if (t == TOK_END) {
                        depth--;
                        if (depth == 0) {
                            end_pos = j;
                            break;
                        }
                    }
                }

                if (take_branch) {
                    /* Execute the 'if' body (up to else or end) */
                    size_t body_end = else_pos > 0 ? else_pos : end_pos;
                    PithSlot if_slot = {
                        .name = NULL,
                        .body_start = if_body_start,
                        .body_end = body_end,
                        .is_cached = false
                    };
                    if (!pith_execute_slot(rt, &if_slot)) {
                        return false;
                    }
                } else if (else_pos > 0) {
                    /* Execute the 'else' body */
                    PithSlot else_slot = {
                        .name = NULL,
                        .body_start = else_pos + 1,
                        .body_end = end_pos,
                        .is_cached = false
                    };
                    if (!pith_execute_slot(rt, &else_slot)) {
                        return false;
                    }
                }
                /* Skip to end */
                i = end_pos;
                break;
            }
                
            case TOK_DO: {
                /* Create a block from here to matching end */
                PithBlock *block = malloc(sizeof(PithBlock));
                block->start = i + 1;
                /* Find matching end */
                int depth = 1;
                size_t j = i + 1;
                while (j < slot->body_end && depth > 0) {
                    if (rt->tokens[j].type == TOK_DO) depth++;
                    if (rt->tokens[j].type == TOK_END) depth--;
                    j++;
                }
                block->end = j - 1;
                pith_push(rt, PITH_BLOCK(block));
                i = j - 1; /* Skip to end of block */
                break;
            }
                
            case TOK_LBRACKET: {
                /* Array: execute code inside [...] and collect stack results */
                /* Remember stack position before executing array contents */
                size_t stack_before = rt->stack_top;

                /* Find matching ] */
                size_t arr_start = i + 1;
                size_t arr_end = arr_start;
                int bracket_depth = 1;
                for (size_t j = arr_start; j < slot->body_end; j++) {
                    if (rt->tokens[j].type == TOK_LBRACKET) bracket_depth++;
                    else if (rt->tokens[j].type == TOK_RBRACKET) {
                        bracket_depth--;
                        if (bracket_depth == 0) {
                            arr_end = j;
                            break;
                        }
                    }
                }

                /* Execute the code inside the array */
                PithSlot arr_slot = {
                    .name = NULL,
                    .body_start = arr_start,
                    .body_end = arr_end,
                    .is_cached = false
                };
                pith_execute_slot(rt, &arr_slot);

                /* Collect everything pushed onto stack into an array */
                size_t num_items = rt->stack_top - stack_before;
                PithArray *arr = pith_array_new();

                /* Items are in reverse order on stack, so collect them properly */
                if (num_items > 0) {
                    PithValue *temp = malloc(num_items * sizeof(PithValue));
                    for (size_t j = 0; j < num_items; j++) {
                        temp[num_items - 1 - j] = pith_pop(rt);
                    }
                    for (size_t j = 0; j < num_items; j++) {
                        pith_array_push(arr, temp[j]);
                    }
                    free(temp);
                }

                pith_push(rt, PITH_ARRAY(arr));
                i = arr_end; /* Skip to ] (loop will increment past it) */
                break;
            }
                
            default:
                break;
        }
        
        if (rt->has_error) return false;
    }
    
    return true;
}

bool pith_execute_block(PithRuntime *rt, PithBlock *block) {
    PithSlot temp = {
        .name = NULL,
        .body_start = block->start,
        .body_end = block->end,
        .is_cached = false
    };
    return pith_execute_slot(rt, &temp);
}

/* ========================================================================
   RUNTIME LIFECYCLE
   ======================================================================== */

PithRuntime* pith_runtime_new(PithFileSystem fs) {
    PithRuntime *rt = malloc(sizeof(PithRuntime));
    memset(rt, 0, sizeof(PithRuntime));
    
    rt->fs = fs;
    rt->root = pith_dict_new("root");
    rt->current_dict = rt->root;
    
    register_builtins(rt);
    
    return rt;
}

void pith_runtime_free(PithRuntime *rt) {
    if (!rt) return;
    
    /* Free stack values */
    for (size_t i = 0; i < rt->stack_top; i++) {
        pith_value_free(rt->stack[i]);
    }
    
    /* Free tokens */
    for (size_t i = 0; i < rt->token_count; i++) {
        free(rt->tokens[i].text);
    }
    
    /* Free root dictionary (dictionaries are now slots with cached VAL_DICT values) */
    pith_dict_free(rt->root);
    
    /* Free project path */
    free(rt->project_path);
    
    /* Free current view */
    pith_view_free(rt->current_view);

    /* Free signals (the actual signals are freed when their containing slots are freed) */
    free(rt->all_signals);

    free(rt);
}

bool pith_runtime_load_project(PithRuntime *rt, const char *path) {
    /* Check if path is a .pith file (direct file execution) */
    size_t len = strlen(path);
    bool is_pith_file = (len > 5 && strcmp(path + len - 5, ".pith") == 0);

    if (is_pith_file && rt->fs.file_exists(path, rt->fs.userdata)) {
        /* Direct file execution - set project path to containing directory */
        char *path_copy = pith_strdup(path);
        char *last_slash = strrchr(path_copy, '/');
        if (last_slash) {
            *last_slash = '\0';
            rt->project_path = path_copy;
        } else {
            free(path_copy);
            rt->project_path = pith_strdup(".");
        }
        return pith_runtime_load_file(rt, path);
    }

    /* Directory-based project loading */
    rt->project_path = pith_strdup(path);

    /* Look for pith/runtime.pith */
    char runtime_path[512];
    snprintf(runtime_path, sizeof(runtime_path), "%s/pith/runtime.pith", path);

    if (rt->fs.file_exists(runtime_path, rt->fs.userdata)) {
        return pith_runtime_load_file(rt, runtime_path);
    }

    /* Create default runtime.pith */
    const char *default_runtime =
        "# Default Pith runtime\n"
        "\n"
        "app:\n"
        "    ui:\n"
        "        [\"Welcome to Pith\" text] vstack\n"
        "    end\n"
        "end\n"
        "\n"
        "# Mount the UI\n"
        "ui:\n"
        "    app\n"
        "end\n";

    /* Ensure directory exists and write default */
    char pith_dir[512];
    snprintf(pith_dir, sizeof(pith_dir), "%s/pith", path);
    rt->fs.write_file(runtime_path, default_runtime, rt->fs.userdata);

    return pith_runtime_load_string(rt, default_runtime, "runtime.pith");
}

bool pith_runtime_load_file(PithRuntime *rt, const char *path) {
    char *source = rt->fs.read_file(path, rt->fs.userdata);
    if (!source) {
        pith_error(rt, "Could not read file: %s", path);
        return false;
    }
    
    bool result = pith_runtime_load_string(rt, source, path);
    free(source);
    return result;
}

/* Find a dictionary by name - looks up slot with cached VAL_DICT in root */
static PithDict* pith_find_dict(PithRuntime *rt, const char *name) {
    /* Look for a slot with this name in root that has a cached dict value */
    for (size_t i = 0; i < rt->root->slot_count; i++) {
        PithSlot *slot = &rt->root->slots[i];
        if (slot->name && strcmp(slot->name, name) == 0) {
            if (slot->is_cached && slot->cached.type == VAL_DICT) {
                return slot->cached.as.dict;
            }
        }
    }
    return NULL;
}

/* Add a dictionary as a slot in the root dictionary */
static bool pith_add_dict_slot(PithRuntime *rt, PithDict *dict, size_t body_start, size_t body_end) {
    if (rt->root->slot_count >= rt->root->slot_capacity) {
        rt->root->slot_capacity = rt->root->slot_capacity ? rt->root->slot_capacity * 2 : 8;
        rt->root->slots = realloc(rt->root->slots, rt->root->slot_capacity * sizeof(PithSlot));
    }

    PithSlot *slot = &rt->root->slots[rt->root->slot_count++];
    slot->name = pith_strdup(dict->name);
    slot->body_start = body_start;
    slot->body_end = body_end;
    slot->is_cached = true;
    slot->cached = PITH_DICT(dict);

    return true;
}

bool pith_runtime_load_string(PithRuntime *rt, const char *source, const char *name) {
    (void)name;

    if (!pith_parse(rt, source)) {
        return false;
    }

    /* Parse dictionaries from token stream */
    size_t i = 0;
    while (i < rt->token_count) {
        PithToken *tok = &rt->tokens[i];

        /* Skip EOF */
        if (tok->type == TOK_EOF) break;

        /* Look for WORD COLON at top level - could be dictionary or root slot */
        if (tok->type == TOK_WORD &&
            i + 1 < rt->token_count &&
            rt->tokens[i + 1].type == TOK_COLON) {

            char *block_name = tok->text;
            size_t block_start = i + 2; /* After name and colon */

            /* Find the block's closing 'end' by tracking nesting */
            /* Track depth for blocks (do/if) and whether there's an open MULTI-LINE slot */
            /* Multi-line slots consume an 'end', single-line slots don't */
            size_t block_end = block_start;
            int depth = 1;
            bool slot_open[64] = {false}; /* slot_open[d] = true if there's a multi-line slot at depth d */

            for (size_t j = block_start; j < rt->token_count; j++) {
                PithTokenType t = rt->tokens[j].type;
                if (t == TOK_DO || t == TOK_IF) {
                    depth++;
                } else if (t == TOK_WORD &&
                           j + 1 < rt->token_count &&
                           rt->tokens[j + 1].type == TOK_COLON) {
                    /* WORD COLON - check if this is a multi-line slot */
                    /* A slot is multi-line if its body contains tokens on different lines */
                    size_t slot_line = rt->tokens[j].line;
                    bool is_multiline = false;

                    /* Look ahead to see if slot body spans multiple lines */
                    for (size_t k = j + 2; k < rt->token_count; k++) {
                        PithTokenType tk = rt->tokens[k].type;
                        /* Stop at END or next WORD COLON */
                        if (tk == TOK_END) break;
                        if (tk == TOK_WORD && k + 1 < rt->token_count &&
                            rt->tokens[k + 1].type == TOK_COLON) break;
                        /* If any token is on a different line, slot is multi-line */
                        if (rt->tokens[k].line > slot_line) {
                            is_multiline = true;
                            break;
                        }
                    }

                    if (is_multiline) {
                        slot_open[depth] = true;
                    }
                    j++; /* Skip the colon */
                } else if (t == TOK_END) {
                    if (slot_open[depth]) {
                        /* This END closes a multi-line slot */
                        slot_open[depth] = false;
                    } else {
                        /* This END closes a block (do/if/dictionary) */
                        depth--;
                        if (depth == 0) {
                            block_end = j;
                            break;
                        }
                    }
                } else if (t == TOK_EOF) {
                    pith_error(rt, "Unexpected end of file in block '%s'", block_name);
                    return false;
                }
            }

            /* Determine if this is a dictionary (has slot definitions) or a root slot */
            /* If first token is WORD and second is COLON, it's a dictionary */
            bool is_dictionary = false;
            if (block_start < block_end &&
                rt->tokens[block_start].type == TOK_WORD &&
                block_start + 1 < block_end &&
                rt->tokens[block_start + 1].type == TOK_COLON) {
                is_dictionary = true;
            }

            if (is_dictionary) {
                /* Create new dictionary and add as slot in root */
                PithDict *dict = pith_dict_new(block_name);
                if (!pith_add_dict_slot(rt, dict, block_start, block_end)) {
                    pith_dict_free(dict);
                    return false;
                }

                /* Parse slots within [block_start, block_end) */
                i = block_start;
                while (i < block_end) {
                    tok = &rt->tokens[i];

                    /* Slot definition: WORD COLON */
                    if (tok->type == TOK_WORD &&
                        i + 1 < block_end &&
                        rt->tokens[i + 1].type == TOK_COLON) {

                        char *slot_name = tok->text;
                        i += 2; /* Skip name and colon */

                        /* Find the end of the slot body */
                        size_t body_start = i;
                        int slot_depth = 0;

                        while (i < block_end) {
                            PithTokenType t = rt->tokens[i].type;

                            /* Track nesting for do...end, if...end blocks */
                            if (t == TOK_DO || t == TOK_IF) {
                                slot_depth++;
                            } else if (t == TOK_END) {
                                if (slot_depth > 0) {
                                    slot_depth--;
                                } else {
                                    /* This 'end' closes our slot (multi-line slot) */
                                    break;
                                }
                            } else if (slot_depth == 0 && t == TOK_WORD &&
                                       i + 1 < block_end &&
                                       rt->tokens[i + 1].type == TOK_COLON) {
                                /* Next slot starts - this slot has implicit end */
                                break;
                            }
                            i++;
                        }

                        size_t body_end = i;

                        /* Skip the slot's 'end' if present (but not dict's end) */
                        if (i < block_end && rt->tokens[i].type == TOK_END) {
                            i++;
                        }

                        /* Add the slot */
                        pith_dict_add_slot(dict, slot_name, body_start, body_end);
                    } else {
                        /* Skip unexpected token */
                        i++;
                    }
                }
            } else {
                /* This is a slot for the root dictionary */
                pith_dict_add_slot(rt->root, block_name, block_start, block_end);
            }

            /* Move past the block's closing 'end' */
            i = block_end + 1;
        } else {
            /* Skip unexpected token at top level */
            i++;
        }
    }

    /* Second pass: resolve parent references for all dictionaries stored as slots */
    for (size_t d = 0; d < rt->root->slot_count; d++) {
        PithSlot *dict_slot = &rt->root->slots[d];
        if (!dict_slot->is_cached || dict_slot->cached.type != VAL_DICT) {
            continue; /* Not a dictionary slot */
        }

        PithDict *dict = dict_slot->cached.as.dict;
        PithSlot *parent_slot = NULL;

        /* Find parent slot in this dict (not following inheritance) */
        for (size_t s = 0; s < dict->slot_count; s++) {
            if (strcmp(dict->slots[s].name, "parent") == 0) {
                parent_slot = &dict->slots[s];
                break;
            }
        }

        if (parent_slot && parent_slot->body_start < parent_slot->body_end) {
            PithToken *parent_tok = &rt->tokens[parent_slot->body_start];
            if (parent_tok->type == TOK_WORD) {
                PithDict *parent = pith_find_dict(rt, parent_tok->text);
                if (parent) {
                    pith_dict_set_parent(dict, parent);
                }
            }
        }
    }

    /* Third pass: cache simple literal slots (single token: string, number, bool, nil) */
    for (size_t d = 0; d < rt->root->slot_count; d++) {
        PithSlot *dict_slot = &rt->root->slots[d];
        if (!dict_slot->is_cached || dict_slot->cached.type != VAL_DICT) {
            continue; /* Not a dictionary slot */
        }

        PithDict *dict = dict_slot->cached.as.dict;
        for (size_t s = 0; s < dict->slot_count; s++) {
            PithSlot *slot = &dict->slots[s];
            if (slot->is_cached) continue; /* Already cached */

            /* Check if slot body is a single token */
            if (slot->body_end - slot->body_start == 1) {
                PithToken *tok = &rt->tokens[slot->body_start];
                switch (tok->type) {
                    case TOK_STRING:
                        slot->is_cached = true;
                        slot->cached = PITH_STRING(pith_strdup(tok->text));
                        break;
                    case TOK_NUMBER:
                        slot->is_cached = true;
                        slot->cached = PITH_NUMBER(atof(tok->text));
                        break;
                    case TOK_TRUE:
                        slot->is_cached = true;
                        slot->cached = PITH_BOOL(true);
                        break;
                    case TOK_FALSE:
                        slot->is_cached = true;
                        slot->cached = PITH_BOOL(false);
                        break;
                    case TOK_NIL:
                        slot->is_cached = true;
                        slot->cached = PITH_NIL();
                        break;
                    default:
                        /* Not a simple literal - leave uncached */
                        break;
                }
            }

            /* Check for signal initialization pattern: <value> signal */
            if (slot->body_end - slot->body_start == 2) {
                PithToken *tok1 = &rt->tokens[slot->body_start];
                PithToken *tok2 = &rt->tokens[slot->body_start + 1];
                if (tok2->type == TOK_WORD && strcmp(tok2->text, "signal") == 0) {
                    PithValue initial = PITH_NIL();
                    bool valid = false;
                    switch (tok1->type) {
                        case TOK_STRING:
                            initial = PITH_STRING(pith_strdup(tok1->text));
                            valid = true;
                            break;
                        case TOK_NUMBER:
                            initial = PITH_NUMBER(atof(tok1->text));
                            valid = true;
                            break;
                        case TOK_TRUE:
                            initial = PITH_BOOL(true);
                            valid = true;
                            break;
                        case TOK_FALSE:
                            initial = PITH_BOOL(false);
                            valid = true;
                            break;
                        case TOK_NIL:
                            initial = PITH_NIL();
                            valid = true;
                            break;
                        default:
                            break;
                    }
                    if (valid) {
                        PithSignal *sig = pith_signal_new(rt, initial);
                        slot->is_cached = true;
                        slot->cached = PITH_SIGNAL(sig);
                    }
                }
            }
        }
    }

    /* Set current dictionary to the file-level root */
    rt->current_dict = rt->root;

    return true;
}

/* Execute a named slot in the root dictionary if it exists */
bool pith_runtime_run_slot(PithRuntime *rt, const char *name) {
    PithSlot *slot = pith_dict_lookup(rt->root, name);
    if (!slot) {
        return false; /* Slot doesn't exist - not an error */
    }

    if (g_debug) {
        fprintf(stderr, "[DEBUG] Executing '%s' slot: tokens %zu-%zu\n",
                name, slot->body_start, slot->body_end);
    }

    return pith_execute_slot(rt, slot);
}

/* Execute the ui slot and set the current view */
bool pith_runtime_mount_ui(PithRuntime *rt) {
    PithSlot *ui_slot = pith_dict_lookup(rt->root, "ui");
    if (!ui_slot) {
        return false; /* No ui slot */
    }

    if (g_debug) {
        fprintf(stderr, "[DEBUG] Executing 'ui' slot: tokens %zu-%zu\n",
                ui_slot->body_start, ui_slot->body_end);
    }

    /* Set UI building context for signal auto-unwrap */
    rt->ui_building = true;

    bool success = pith_execute_slot(rt, ui_slot);

    rt->ui_building = false;

    if (!success) {
        return false;
    }

    /* If a view is on the stack, use it as the root view */
    if (pith_stack_has(rt, 1)) {
        PithValue v = pith_peek(rt);
        if (PITH_IS_VIEW(v)) {
            rt->current_view = pith_pop(rt).as.view;
            if (g_debug) {
                fprintf(stderr, "[DEBUG] Root view set from ui slot\n");
            }
            return true;
        }
    }

    return false;
}

void pith_runtime_handle_event(PithRuntime *rt, PithEvent event) {
    /* Find and execute appropriate event handler */
    const char *handler_name = NULL;
    
    switch (event.type) {
        case EVENT_KEY:
            handler_name = "on-key";
            /* Push key info onto stack */
            pith_push(rt, PITH_NUMBER(event.as.key.key_code));
            break;
            
        case EVENT_CLICK:
            handler_name = "on-click";
            break;
            
        case EVENT_FILE_CHANGE:
            handler_name = "on-file-change";
            pith_push(rt, PITH_STRING(pith_strdup(event.as.file_change.path)));
            break;
            
        default:
            return;
    }
    
    if (handler_name && rt->current_dict) {
        PithSlot *slot = pith_dict_lookup(rt->current_dict, handler_name);
        if (slot) {
            pith_execute_slot(rt, slot);
        }
    }
}

PithView* pith_runtime_get_view(PithRuntime *rt) {
    /* Return the view that was set by top-level code execution */
    /* In the future, this could re-execute for reactivity */
    return rt->current_view;
}

/* ========================================================================
   DEBUG
   ======================================================================== */

static const char* token_type_name(PithTokenType t) {
    switch (t) {
        case TOK_EOF: return "EOF";
        case TOK_WORD: return "WORD";
        case TOK_NUMBER: return "NUMBER";
        case TOK_STRING: return "STRING";
        case TOK_COLON: return "COLON";
        case TOK_DOT: return "DOT";
        case TOK_LBRACKET: return "LBRACKET";
        case TOK_RBRACKET: return "RBRACKET";
        case TOK_LBRACE: return "LBRACE";
        case TOK_RBRACE: return "RBRACE";
        case TOK_END: return "END";
        case TOK_IF: return "IF";
        case TOK_ELSE: return "ELSE";
        case TOK_DO: return "DO";
        case TOK_TRUE: return "TRUE";
        case TOK_FALSE: return "FALSE";
        case TOK_NIL: return "NIL";
        default: return "UNKNOWN";
    }
}

void pith_debug_print_state(PithRuntime *rt) {
    fprintf(stderr, "\n=== PITH DEBUG STATE ===\n\n");

    fprintf(stderr, "Token count: %zu\n", rt->token_count);
    fprintf(stderr, "Root slot count: %zu\n", rt->root->slot_count);
    fprintf(stderr, "Current dict: %s\n", rt->current_dict ? rt->current_dict->name : "(null)");

    fprintf(stderr, "\n--- Root Slots ---\n");
    for (size_t i = 0; i < rt->root->slot_count; i++) {
        PithSlot *root_slot = &rt->root->slots[i];

        if (root_slot->is_cached && root_slot->cached.type == VAL_DICT) {
            /* This slot is a dictionary */
            PithDict *dict = root_slot->cached.as.dict;
            fprintf(stderr, "\n[%zu] %s (dictionary)", i, dict->name ? dict->name : "(unnamed)");
            if (dict->parent) {
                fprintf(stderr, " : %s", dict->parent->name ? dict->parent->name : "(unnamed)");
            }
            fprintf(stderr, "\n");

            for (size_t s = 0; s < dict->slot_count; s++) {
                PithSlot *slot = &dict->slots[s];
                fprintf(stderr, "    %s: [tokens %zu-%zu]",
                        slot->name, slot->body_start, slot->body_end);

                /* Print first few tokens of body */
                fprintf(stderr, " = ");
                size_t max_tokens = 5;
                for (size_t t = slot->body_start; t < slot->body_end && t < slot->body_start + max_tokens; t++) {
                    PithToken *tok = &rt->tokens[t];
                    if (tok->text) {
                        fprintf(stderr, "%s ", tok->text);
                    } else {
                        fprintf(stderr, "<%s> ", token_type_name(tok->type));
                    }
                }
                if (slot->body_end - slot->body_start > max_tokens) {
                    fprintf(stderr, "...");
                }
                fprintf(stderr, "\n");
            }
        } else {
            /* Regular slot */
            fprintf(stderr, "\n[%zu] %s: [tokens %zu-%zu]", i, root_slot->name, root_slot->body_start, root_slot->body_end);
            fprintf(stderr, " = ");
            size_t max_tokens = 5;
            for (size_t t = root_slot->body_start; t < root_slot->body_end && t < root_slot->body_start + max_tokens; t++) {
                PithToken *tok = &rt->tokens[t];
                if (tok->text) {
                    fprintf(stderr, "%s ", tok->text);
                } else {
                    fprintf(stderr, "<%s> ", token_type_name(tok->type));
                }
            }
            if (root_slot->body_end - root_slot->body_start > max_tokens) {
                fprintf(stderr, "...");
            }
            fprintf(stderr, "\n");
        }
    }

    fprintf(stderr, "\n--- Current Dict Slots ---\n");
    if (rt->current_dict) {
        PithSlot *ui_slot = pith_dict_lookup(rt->current_dict, "ui");
        if (ui_slot) {
            fprintf(stderr, "Found 'ui' slot: tokens %zu-%zu\n", ui_slot->body_start, ui_slot->body_end);
            fprintf(stderr, "UI slot body tokens:\n");
            for (size_t t = ui_slot->body_start; t < ui_slot->body_end; t++) {
                PithToken *tok = &rt->tokens[t];
                fprintf(stderr, "  [%zu] %s", t, token_type_name(tok->type));
                if (tok->text) {
                    fprintf(stderr, " \"%s\"", tok->text);
                }
                fprintf(stderr, "\n");
            }
        } else {
            fprintf(stderr, "No 'ui' slot found in current dict!\n");
        }
    }

    fprintf(stderr, "\n========================\n\n");
}

static const char* view_type_name(PithViewType t) {
    switch (t) {
        case VIEW_TEXT: return "TEXT";
        case VIEW_TEXTFIELD: return "TEXTFIELD";
        case VIEW_BUTTON: return "BUTTON";
        case VIEW_TEXTURE: return "TEXTURE";
        case VIEW_VSTACK: return "VSTACK";
        case VIEW_HSTACK: return "HSTACK";
        case VIEW_SPACER: return "SPACER";
        default: return "UNKNOWN";
    }
}

void pith_debug_print_view(PithView *view, int indent) {
    if (!view) {
        fprintf(stderr, "%*s(null view)\n", indent * 2, "");
        return;
    }

    fprintf(stderr, "%*s%s", indent * 2, "", view_type_name(view->type));

    switch (view->type) {
        case VIEW_TEXT:
            fprintf(stderr, ": \"%s\"", view->as.text.content ? view->as.text.content : "(null)");
            break;
        case VIEW_BUTTON:
            fprintf(stderr, ": \"%s\"", view->as.button.label ? view->as.button.label : "(null)");
            break;
        case VIEW_VSTACK:
        case VIEW_HSTACK:
            fprintf(stderr, " (%zu children)", view->as.stack.count);
            break;
        default:
            break;
    }
    fprintf(stderr, "\n");

    /* Print children for stacks */
    if (view->type == VIEW_VSTACK || view->type == VIEW_HSTACK) {
        for (size_t i = 0; i < view->as.stack.count; i++) {
            pith_debug_print_view(view->as.stack.children[i], indent + 1);
        }
    }
}
