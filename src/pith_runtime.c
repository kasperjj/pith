/*
 * pith_runtime.c - Platform-independent Pith interpreter
 */

#include "pith_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

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
            /* These are references, don't deep copy */
            return value;
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
    view->as.textfield.content = pith_strdup(content);
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
            free(view->as.textfield.content);
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
        const char *start = lex->current;
        while (lexer_peek(lex) != '\0' && lexer_peek(lex) != '"') {
            if (lexer_peek(lex) == '\\') {
                lexer_advance(lex); /* skip escape */
            }
            lexer_advance(lex);
        }
        size_t len = lex->current - start;
        token.text = malloc(len + 1);
        memcpy(token.text, start, len);
        token.text[len] = '\0';
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
    
    /* Arithmetic */
    {"add", builtin_add},
    {"subtract", builtin_subtract},
    {"multiply", builtin_multiply},
    {"divide", builtin_divide},
    
    /* Comparison */
    {"=", builtin_equal},
    {"<", builtin_less},
    {">", builtin_greater},
    
    /* Logic */
    {"and", builtin_and},
    {"or", builtin_or},
    {"not", builtin_not},
    
    /* Strings/Arrays */
    {"length", builtin_length},
    
    /* Debug */
    {"print", builtin_print},
    
    /* UI */
    {"text", builtin_text},
    {"vstack", builtin_vstack},
    {"hstack", builtin_hstack},
    {"spacer", builtin_spacer},

    /* Functional */
    {"map", builtin_map},

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
            /* If slot is a cached dictionary, execute its ui slot */
            if (slot->is_cached && slot->cached.type == VAL_DICT) {
                PithDict *dict = slot->cached.as.dict;
                PithSlot *ui_slot = pith_dict_lookup(dict, "ui");
                if (ui_slot) {
                    PithDict *saved_dict = rt->current_dict;
                    rt->current_dict = dict;
                    bool result = pith_execute_slot(rt, ui_slot);
                    rt->current_dict = saved_dict;
                    g_exec_depth--;
                    return result;
                }
            } else {
                bool result = pith_execute_slot(rt, slot);
                g_exec_depth--;
                return result;
            }
        }
    }

    /* Check if it's a dictionary name in root - execute its ui slot */
    PithDict *dict = pith_find_dict(rt, name);
    if (dict) {
        PithSlot *ui_slot = pith_dict_lookup(dict, "ui");
        if (ui_slot) {
            PithDict *saved_dict = rt->current_dict;
            rt->current_dict = dict;
            bool result = pith_execute_slot(rt, ui_slot);
            rt->current_dict = saved_dict;
            g_exec_depth--;
            return result;
        }
    }

    g_exec_depth--;
    pith_error(rt, "Unknown word: %s", name);
    return false;
}

bool pith_execute_slot(PithRuntime *rt, PithSlot *slot) {
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
                /* Check for dot-access: word.slot */
                if (i + 2 < slot->body_end &&
                    rt->tokens[i + 1].type == TOK_DOT &&
                    rt->tokens[i + 2].type == TOK_WORD) {
                    /* Dot access: dict.slot */
                    const char *dict_name = tok->text;
                    const char *slot_name = rt->tokens[i + 2].text;
                    PithDict *dict = pith_find_dict(rt, dict_name);
                    if (dict) {
                        PithSlot *target_slot = pith_dict_lookup(dict, slot_name);
                        if (target_slot) {
                            PithDict *saved_dict = rt->current_dict;
                            rt->current_dict = dict;
                            if (!pith_execute_slot(rt, target_slot)) {
                                rt->current_dict = saved_dict;
                                return false;
                            }
                            rt->current_dict = saved_dict;
                        } else {
                            pith_error(rt, "Unknown slot '%s' in dict '%s'", slot_name, dict_name);
                            return false;
                        }
                    } else {
                        pith_error(rt, "Unknown dictionary: %s", dict_name);
                        return false;
                    }
                    i += 2; /* Skip DOT and second WORD */
                } else {
                    if (!pith_execute_word(rt, tok->text)) {
                        return false;
                    }
                }
                break;
                
            case TOK_IF: {
                /* Simple if implementation - find matching end/else */
                /* This is simplified - a real implementation would be more robust */
                if (!pith_stack_has(rt, 1)) {
                    pith_error(rt, "if requires condition on stack");
                    return false;
                }
                PithValue cond = pith_pop(rt);
                bool take_branch = cond.as.boolean;
                /* TODO: implement proper control flow */
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

    if (!pith_execute_slot(rt, ui_slot)) {
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
