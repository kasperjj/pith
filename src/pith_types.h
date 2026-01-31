/*
 * pith_types.h - Core types shared between runtime and UI
 * 
 * This file defines the data structures that form the interface
 * between the platform-independent runtime and the platform-specific UI.
 */

#ifndef PITH_TYPES_H
#define PITH_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ========================================================================
   VALUE TYPES
   ======================================================================== */

typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_STRING,
    VAL_ARRAY,
    VAL_MAP,
    VAL_BLOCK,      /* Anonymous block (do ... end) */
    VAL_VIEW,       /* UI view */
    VAL_DICT,       /* Dictionary/component reference */
    VAL_GAPBUF,     /* Gap buffer for text editing */
    VAL_SIGNAL,     /* Reactive signal */
} PithValueType;

/* Forward declarations */
typedef struct PithValue PithValue;
typedef struct PithArray PithArray;
typedef struct PithMap PithMap;
typedef struct PithBlock PithBlock;
typedef struct PithView PithView;
typedef struct PithDict PithDict;
typedef struct PithSlot PithSlot;
typedef struct PithGapBuffer PithGapBuffer;
typedef struct PithSignal PithSignal;

/* Anonymous block - stores word indices to execute */
struct PithBlock {
    size_t start;   /* Start index in token stream */
    size_t end;     /* End index in token stream */
};

/* The universal value type - defined early so it can be embedded in other structs */
struct PithValue {
    PithValueType type;
    union {
        bool boolean;
        double number;
        char *string;
        PithArray *array;
        PithMap *map;
        PithBlock *block;
        PithView *view;
        PithDict *dict;
        PithGapBuffer *gapbuf;
        PithSignal *signal;
    } as;
};

/* Dynamic array of values */
struct PithArray {
    PithValue *items;
    size_t length;
    size_t capacity;
};

/* Key-value pair for maps */
typedef struct {
    char *key;
    PithValue value;
} PithMapEntry;

/* Hash map */
struct PithMap {
    PithMapEntry *entries;
    size_t length;
    size_t capacity;
};

/* Gap buffer for efficient text editing
 * Structure: [pre-gap text][gap][post-gap text]
 * The cursor is always at the start of the gap.
 */
struct PithGapBuffer {
    char *buffer;       /* The underlying buffer */
    size_t capacity;    /* Total buffer size */
    size_t gap_start;   /* Start of gap (cursor position in content) */
    size_t gap_end;     /* End of gap (exclusive) */
};

/* Reactive signal - wraps a value and tracks dependencies
 * When a signal is read during UI building, the reading slot subscribes.
 * When a signal is written, all subscribers are marked dirty for re-render.
 */
struct PithSignal {
    PithValue value;            /* The wrapped value */
    PithSlot **subscribers;     /* Slots that depend on this signal */
    size_t subscriber_count;
    size_t subscriber_capacity;
    bool dirty;                 /* Needs re-render */
};

/* ========================================================================
   VIEW TYPES (UI)
   ======================================================================== */

typedef enum {
    VIEW_TEXT,
    VIEW_TEXTFIELD,
    VIEW_TEXTAREA,
    VIEW_BUTTON,
    VIEW_TEXTURE,
    VIEW_VSTACK,
    VIEW_HSTACK,
    VIEW_SPACER,
} PithViewType;

/* Style properties - all optional (use parent if not set) */
typedef struct {
    bool has_color;
    uint32_t color;             /* RGBA */
    
    bool has_background;
    uint32_t background;        /* RGBA */
    
    bool has_bold;
    bool bold;
    
    bool has_border;
    char *border;               /* "all", "top", "left right", etc. */
    
    bool has_padding;
    int padding;
    
    bool has_gap;
    int gap;
    
    bool has_width;
    int width;                  /* In cells, 0 = auto */
    
    bool has_height;
    int height;                 /* In cells, 0 = auto */
    
    bool fill;                  /* Expand to available space */
} PithStyle;

/* A renderable view */
struct PithView {
    PithViewType type;
    PithStyle style;

    union {
        /* VIEW_TEXT */
        struct {
            char *content;
        } text;
        
        /* VIEW_TEXTFIELD */
        struct {
            PithGapBuffer *buffer;
            PithBlock *on_change;
            PithSignal *source_signal;  /* Signal to update on blur */
        } textfield;

        /* VIEW_TEXTAREA */
        struct {
            PithGapBuffer *buffer;
            PithBlock *on_change;
            PithSignal *source_signal;  /* Signal to update on blur */
            int scroll_offset;    /* First visible line for scrolling */
            int visible_height;   /* Cached visible height from last render */
        } textarea;

        /* VIEW_BUTTON */
        struct {
            char *label;
            PithBlock *on_click;
        } button;
        
        /* VIEW_TEXTURE */
        struct {
            char *path;
        } texture;
        
        /* VIEW_VSTACK / VIEW_HSTACK */
        struct {
            PithView **children;
            size_t count;
        } stack;
    } as;

    /* Cached render position (set during rendering for click handling) */
    int render_x;
    int render_y;
    int render_w;
    int render_h;
};

/* ========================================================================
   DICTIONARY TYPES
   ======================================================================== */

/* A slot in a dictionary - can be data or code */
struct PithSlot {
    char *name;
    
    /* The word body - sequence of tokens to execute */
    /* For simple data slots, this is just the literal value */
    size_t body_start;
    size_t body_end;
    
    /* Cached value (for pure data slots) */
    bool is_cached;
    PithValue cached;
};

/* A dictionary (component) */
struct PithDict {
    char *name;
    PithDict *parent;           /* For inheritance chain */
    
    PithSlot *slots;
    size_t slot_count;
    size_t slot_capacity;
};

/* ========================================================================
   EVENT TYPES
   ======================================================================== */

typedef enum {
    EVENT_NONE,
    EVENT_KEY,
    EVENT_CLICK,
    EVENT_TEXT_INPUT,
    EVENT_FILE_CHANGE,
    EVENT_TICK,
} PithEventType;

typedef struct {
    PithEventType type;
    union {
        /* EVENT_KEY */
        struct {
            int key_code;
            bool ctrl;
            bool alt;
            bool shift;
            bool cmd;           /* macOS command key */
        } key;
        
        /* EVENT_CLICK */
        struct {
            int x;              /* Cell coordinates */
            int y;
            int button;         /* 0=left, 1=right, 2=middle */
            PithView *target;   /* Which view was clicked */
        } click;
        
        /* EVENT_TEXT_INPUT */
        struct {
            char *text;         /* UTF-8 string */
        } text_input;
        
        /* EVENT_FILE_CHANGE */
        struct {
            char *path;
        } file_change;
    } as;
} PithEvent;

/* ========================================================================
   HELPER MACROS
   ======================================================================== */

/* Create values */
#define PITH_NIL()          ((PithValue){ .type = VAL_NIL })
#define PITH_BOOL(v)        ((PithValue){ .type = VAL_BOOL, .as.boolean = (v) })
#define PITH_NUMBER(v)      ((PithValue){ .type = VAL_NUMBER, .as.number = (v) })
#define PITH_STRING(v)      ((PithValue){ .type = VAL_STRING, .as.string = (v) })
#define PITH_ARRAY(v)       ((PithValue){ .type = VAL_ARRAY, .as.array = (v) })
#define PITH_MAP(v)         ((PithValue){ .type = VAL_MAP, .as.map = (v) })
#define PITH_BLOCK(v)       ((PithValue){ .type = VAL_BLOCK, .as.block = (v) })
#define PITH_VIEW(v)        ((PithValue){ .type = VAL_VIEW, .as.view = (v) })
#define PITH_DICT(v)        ((PithValue){ .type = VAL_DICT, .as.dict = (v) })
#define PITH_GAPBUF(v)      ((PithValue){ .type = VAL_GAPBUF, .as.gapbuf = (v) })
#define PITH_SIGNAL(v)      ((PithValue){ .type = VAL_SIGNAL, .as.signal = (v) })

/* Type checking */
#define PITH_IS_NIL(v)      ((v).type == VAL_NIL)
#define PITH_IS_BOOL(v)     ((v).type == VAL_BOOL)
#define PITH_IS_NUMBER(v)   ((v).type == VAL_NUMBER)
#define PITH_IS_STRING(v)   ((v).type == VAL_STRING)
#define PITH_IS_ARRAY(v)    ((v).type == VAL_ARRAY)
#define PITH_IS_MAP(v)      ((v).type == VAL_MAP)
#define PITH_IS_BLOCK(v)    ((v).type == VAL_BLOCK)
#define PITH_IS_VIEW(v)     ((v).type == VAL_VIEW)
#define PITH_IS_DICT(v)     ((v).type == VAL_DICT)
#define PITH_IS_GAPBUF(v)   ((v).type == VAL_GAPBUF)
#define PITH_IS_SIGNAL(v)   ((v).type == VAL_SIGNAL)

#endif /* PITH_TYPES_H */
