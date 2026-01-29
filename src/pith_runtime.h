/*
 * pith_runtime.h - Platform-independent Pith interpreter
 * 
 * This is the core interpreter that parses .pith files, manages
 * the stack and dictionaries, and executes words. It has no
 * platform dependencies and produces View trees for the UI to render.
 */

#ifndef PITH_RUNTIME_H
#define PITH_RUNTIME_H

#include "pith_types.h"

/* ========================================================================
   RUNTIME CONFIGURATION
   ======================================================================== */

#define PITH_STACK_MAX      256
#define PITH_DICT_MAX       64
#define PITH_TOKEN_MAX      4096
#define PITH_ERROR_MAX      256

/* ========================================================================
   TOKEN TYPES (for parser)
   ======================================================================== */

typedef enum {
    TOK_EOF,
    TOK_WORD,           /* identifier */
    TOK_NUMBER,         /* 42, 3.14 */
    TOK_STRING,         /* "hello" */
    TOK_COLON,          /* : */
    TOK_DOT,            /* . */
    TOK_LBRACKET,       /* [ */
    TOK_RBRACKET,       /* ] */
    TOK_LBRACE,         /* { */
    TOK_RBRACE,         /* } */
    TOK_END,            /* end keyword */
    TOK_IF,             /* if keyword */
    TOK_ELSE,           /* else keyword */
    TOK_DO,             /* do keyword */
    TOK_TRUE,           /* true */
    TOK_FALSE,          /* false */
    TOK_NIL,            /* nil */
} PithTokenType;

typedef struct {
    PithTokenType type;
    char *text;
    size_t line;
    size_t column;
} PithToken;

/* ========================================================================
   FILE SYSTEM CALLBACKS
   
   The runtime uses these callbacks for file operations, making it
   easy to port to different platforms or mock for testing.
   ======================================================================== */

typedef struct {
    /* Read entire file contents. Caller must free returned string. */
    char* (*read_file)(const char *path, void *userdata);
    
    /* Write string to file. Returns true on success. */
    bool (*write_file)(const char *path, const char *contents, void *userdata);
    
    /* Check if file exists */
    bool (*file_exists)(const char *path, void *userdata);
    
    /* List directory contents. Returns array of paths. Caller must free. */
    char** (*list_dir)(const char *path, size_t *count, void *userdata);
    
    /* User data passed to all callbacks */
    void *userdata;
} PithFileSystem;

/* ========================================================================
   RUNTIME STATE
   ======================================================================== */

typedef struct {
    /* Value stack */
    PithValue stack[PITH_STACK_MAX];
    size_t stack_top;
    
    /* Token stream (from parser) */
    PithToken tokens[PITH_TOKEN_MAX];
    size_t token_count;
    
    /* Dictionaries */
    PithDict *dicts[PITH_DICT_MAX];
    size_t dict_count;
    
    /* Root dictionary (built-in words) */
    PithDict *root;
    
    /* Current execution context */
    PithDict *current_dict;
    
    /* Project path */
    char *project_path;
    
    /* File system callbacks */
    PithFileSystem fs;
    
    /* Error state */
    bool has_error;
    char error[PITH_ERROR_MAX];
    
    /* Current view tree (result of evaluating ui slot) */
    PithView *current_view;
    
} PithRuntime;

/* ========================================================================
   RUNTIME API
   ======================================================================== */

/* Initialize a new runtime with the given file system callbacks */
PithRuntime* pith_runtime_new(PithFileSystem fs);

/* Free runtime and all associated memory */
void pith_runtime_free(PithRuntime *rt);

/* Load a project from a directory path */
bool pith_runtime_load_project(PithRuntime *rt, const char *path);

/* Parse a .pith file and add its dictionaries to the runtime */
bool pith_runtime_load_file(PithRuntime *rt, const char *path);

/* Parse a string of pith code */
bool pith_runtime_load_string(PithRuntime *rt, const char *source, const char *name);

/* Process an event (key press, click, etc.) */
void pith_runtime_handle_event(PithRuntime *rt, PithEvent event);

/* Get the current view tree to render */
PithView* pith_runtime_get_view(PithRuntime *rt);

/* ========================================================================
   STACK OPERATIONS
   ======================================================================== */

/* Push a value onto the stack */
bool pith_push(PithRuntime *rt, PithValue value);

/* Pop a value from the stack */
PithValue pith_pop(PithRuntime *rt);

/* Peek at the top of the stack without popping */
PithValue pith_peek(PithRuntime *rt);

/* Check if stack has at least n items */
bool pith_stack_has(PithRuntime *rt, size_t n);

/* ========================================================================
   DICTIONARY OPERATIONS
   ======================================================================== */

/* Create a new dictionary */
PithDict* pith_dict_new(const char *name);

/* Free a dictionary */
void pith_dict_free(PithDict *dict);

/* Set the parent of a dictionary */
void pith_dict_set_parent(PithDict *dict, PithDict *parent);

/* Add a slot to a dictionary */
bool pith_dict_add_slot(PithDict *dict, const char *name, size_t body_start, size_t body_end);

/* Look up a slot by name, following parent chain */
PithSlot* pith_dict_lookup(PithDict *dict, const char *name);

/* ========================================================================
   EXECUTION
   ======================================================================== */

/* Execute a slot's body */
bool pith_execute_slot(PithRuntime *rt, PithSlot *slot);

/* Execute a word by name in current context */
bool pith_execute_word(PithRuntime *rt, const char *name);

/* Execute a block */
bool pith_execute_block(PithRuntime *rt, PithBlock *block);

/* ========================================================================
   BUILT-IN WORDS
   
   These are registered in the root dictionary during initialization.
   Each takes the runtime and operates on the stack.
   ======================================================================== */

typedef bool (*PithBuiltinFn)(PithRuntime *rt);

/* Register a built-in word */
bool pith_register_builtin(PithRuntime *rt, const char *name, PithBuiltinFn fn);

/* ========================================================================
   VALUE HELPERS
   ======================================================================== */

/* Create a deep copy of a value */
PithValue pith_value_copy(PithValue value);

/* Free a value's memory */
void pith_value_free(PithValue value);

/* Convert value to string for display */
char* pith_value_to_string(PithValue value);

/* Check value equality */
bool pith_value_equal(PithValue a, PithValue b);

/* ========================================================================
   ARRAY HELPERS
   ======================================================================== */

PithArray* pith_array_new(void);
void pith_array_free(PithArray *array);
void pith_array_push(PithArray *array, PithValue value);
PithValue pith_array_pop(PithArray *array);
PithValue pith_array_get(PithArray *array, size_t index);

/* ========================================================================
   MAP HELPERS
   ======================================================================== */

PithMap* pith_map_new(void);
void pith_map_free(PithMap *map);
void pith_map_set(PithMap *map, const char *key, PithValue value);
PithValue* pith_map_get(PithMap *map, const char *key);
bool pith_map_has(PithMap *map, const char *key);

/* ========================================================================
   VIEW HELPERS
   ======================================================================== */

PithView* pith_view_text(const char *content);
PithView* pith_view_textfield(const char *content, PithBlock *on_change);
PithView* pith_view_button(const char *label, PithBlock *on_click);
PithView* pith_view_vstack(PithView **children, size_t count);
PithView* pith_view_hstack(PithView **children, size_t count);
void pith_view_free(PithView *view);

/* ========================================================================
   ERROR HANDLING
   ======================================================================== */

/* Set an error message */
void pith_error(PithRuntime *rt, const char *fmt, ...);

/* Clear error state */
void pith_clear_error(PithRuntime *rt);

/* Get error message (or NULL if no error) */
const char* pith_get_error(PithRuntime *rt);

/* ========================================================================
   DEBUG
   ======================================================================== */

/* Print runtime state (dictionaries, slots, etc.) */
void pith_debug_print_state(PithRuntime *rt);

/* Print view hierarchy */
void pith_debug_print_view(PithView *view, int indent);

#endif /* PITH_RUNTIME_H */
