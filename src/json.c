/*
 * json.c — Minimal recursive-descent JSON parser and serializer.
 *
 * Supports objects, arrays, strings (with escape sequences),
 * numbers, booleans, and null.  Produces pretty-printed output
 * with 2-space indentation.
 *
 * Part of the ATM Simulator project.
 */

#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers (all static)                                      */
/* ------------------------------------------------------------------ */

/* Allocate a zeroed-out JsonNode. */
static JsonNode *node_new(JsonType type)
{
    JsonNode *n = (JsonNode *)calloc(1, sizeof(JsonNode));
    if (n) {
        n->type = type;
    }
    return n;
}

/* Advance *p past any whitespace characters. */
static void skip_ws(const char **p)
{
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

/* Forward declaration — the top-level recursive parser. */
static JsonNode *parse_value(const char **p);

/* ------------------------------------------------------------------ */
/*  String parser                                                      */
/* ------------------------------------------------------------------ */

/*
 * Parse a JSON string (the opening '"' must be the current character).
 * Handles the escape sequences: \" \\ \n \t
 * Returns a malloc'd C string; advances *p past the closing '"'.
 */
static char *parse_string_raw(const char **p)
{
    /* Skip opening quote */
    (*p)++;

    /* First pass: measure the length we need. */
    const char *start = *p;
    size_t len = 0;
    const char *s = start;
    while (*s && *s != '"') {
        if (*s == '\\') {
            s++;          /* skip the backslash */
            if (*s) s++;  /* skip the escaped char */
            len++;
        } else {
            s++;
            len++;
        }
    }

    /* Allocate the result buffer. */
    char *buf = (char *)malloc(len + 1);
    if (!buf) return NULL;

    /* Second pass: copy with escape translation. */
    size_t i = 0;
    while (**p && **p != '"') {
        if (**p == '\\') {
            (*p)++;
            switch (**p) {
                case '"':  buf[i++] = '"';  break;
                case '\\': buf[i++] = '\\'; break;
                case 'n':  buf[i++] = '\n'; break;
                case 't':  buf[i++] = '\t'; break;
                case '/':  buf[i++] = '/';  break;
                case 'b':  buf[i++] = '\b'; break;
                case 'f':  buf[i++] = '\f'; break;
                case 'r':  buf[i++] = '\r'; break;
                default:   buf[i++] = **p;  break;   /* keep unknown escapes as-is */
            }
            (*p)++;
        } else {
            buf[i++] = **p;
            (*p)++;
        }
    }
    buf[i] = '\0';

    /* Skip closing quote */
    if (**p == '"') (*p)++;

    return buf;
}

/* Parse a JSON string and return it wrapped in a JSON_STRING node. */
static JsonNode *parse_string(const char **p)
{
    JsonNode *node = node_new(JSON_STRING);
    if (!node) return NULL;
    node->string = parse_string_raw(p);
    return node;
}

/* ------------------------------------------------------------------ */
/*  Number parser                                                      */
/* ------------------------------------------------------------------ */

static JsonNode *parse_number(const char **p)
{
    char *end = NULL;
    double val = strtod(*p, &end);
    if (end == *p) return NULL;   /* not a valid number */

    JsonNode *node = node_new(JSON_NUMBER);
    if (!node) return NULL;
    node->number = val;
    *p = end;
    return node;
}

/* ------------------------------------------------------------------ */
/*  Literal parsers (true, false, null)                                */
/* ------------------------------------------------------------------ */

static JsonNode *parse_true(const char **p)
{
    if (strncmp(*p, "true", 4) == 0) {
        *p += 4;
        JsonNode *node = node_new(JSON_BOOL);
        if (node) node->boolean = 1;
        return node;
    }
    return NULL;
}

static JsonNode *parse_false(const char **p)
{
    if (strncmp(*p, "false", 5) == 0) {
        *p += 5;
        JsonNode *node = node_new(JSON_BOOL);
        if (node) node->boolean = 0;
        return node;
    }
    return NULL;
}

static JsonNode *parse_null(const char **p)
{
    if (strncmp(*p, "null", 4) == 0) {
        *p += 4;
        return node_new(JSON_NULL);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Array parser                                                       */
/* ------------------------------------------------------------------ */

static JsonNode *parse_array(const char **p)
{
    /* Skip opening '[' */
    (*p)++;

    JsonNode *node = node_new(JSON_ARRAY);
    if (!node) return NULL;

    skip_ws(p);
    if (**p == ']') {
        (*p)++;
        return node;   /* empty array */
    }

    /* Parse the first element. */
    JsonNode *tail = NULL;
    while (1) {
        skip_ws(p);
        JsonNode *elem = parse_value(p);
        if (!elem) break;

        /* Append to child list. */
        if (!node->child) {
            node->child = elem;
        } else {
            tail->next = elem;
        }
        tail = elem;

        skip_ws(p);
        if (**p == ',') {
            (*p)++;   /* skip comma, continue */
        } else {
            break;
        }
    }

    skip_ws(p);
    if (**p == ']') (*p)++;

    return node;
}

/* ------------------------------------------------------------------ */
/*  Object parser                                                      */
/* ------------------------------------------------------------------ */

static JsonNode *parse_object(const char **p)
{
    /* Skip opening '{' */
    (*p)++;

    JsonNode *node = node_new(JSON_OBJECT);
    if (!node) return NULL;

    skip_ws(p);
    if (**p == '}') {
        (*p)++;
        return node;   /* empty object */
    }

    JsonNode *tail = NULL;
    while (1) {
        skip_ws(p);
        if (**p != '"') break;   /* keys must be strings */

        /* Parse the key. */
        char *key = parse_string_raw(p);

        skip_ws(p);
        if (**p == ':') (*p)++;   /* skip colon */

        /* Parse the value. */
        skip_ws(p);
        JsonNode *val = parse_value(p);
        if (!val) {
            free(key);
            break;
        }
        val->key = key;

        /* Append to child list. */
        if (!node->child) {
            node->child = val;
        } else {
            tail->next = val;
        }
        tail = val;

        skip_ws(p);
        if (**p == ',') {
            (*p)++;   /* skip comma, continue */
        } else {
            break;
        }
    }

    skip_ws(p);
    if (**p == '}') (*p)++;

    return node;
}

/* ------------------------------------------------------------------ */
/*  Top-level value dispatcher                                         */
/* ------------------------------------------------------------------ */

static JsonNode *parse_value(const char **p)
{
    skip_ws(p);

    switch (**p) {
        case '"': return parse_string(p);
        case '{': return parse_object(p);
        case '[': return parse_array(p);
        case 't': return parse_true(p);
        case 'f': return parse_false(p);
        case 'n': return parse_null(p);
        default:
            /* Must be a number (digit, '-', '+', '.') */
            if (**p == '-' || **p == '+' || **p == '.' || isdigit((unsigned char)**p)) {
                return parse_number(p);
            }
            return NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  Serializer helpers                                                 */
/* ------------------------------------------------------------------ */

/*
 * A simple dynamic string buffer used during serialization.
 */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Buffer;

static void buf_init(Buffer *b)
{
    b->cap  = 256;
    b->len  = 0;
    b->data = (char *)malloc(b->cap);
    if (b->data) b->data[0] = '\0';
}

/* Ensure there is room for at least `extra` more bytes. */
static void buf_grow(Buffer *b, size_t extra)
{
    while (b->len + extra + 1 > b->cap) {
        b->cap *= 2;
        b->data = (char *)realloc(b->data, b->cap);
    }
}

/* Append a C string to the buffer. */
static void buf_append(Buffer *b, const char *s)
{
    size_t slen = strlen(s);
    buf_grow(b, slen);
    memcpy(b->data + b->len, s, slen);
    b->len += slen;
    b->data[b->len] = '\0';
}

/* Append a single character. */
static void buf_append_char(Buffer *b, char c)
{
    buf_grow(b, 1);
    b->data[b->len++] = c;
    b->data[b->len]   = '\0';
}

/* Append `count` repetitions of a 2-space indent. */
static void buf_indent(Buffer *b, int depth)
{
    for (int i = 0; i < depth; i++) {
        buf_append(b, "  ");
    }
}

/*
 * Append a JSON-escaped version of `s` (including surrounding quotes).
 */
static void buf_append_escaped(Buffer *b, const char *s)
{
    buf_append_char(b, '"');
    if (s) {
        for (const char *c = s; *c; c++) {
            switch (*c) {
                case '"':  buf_append(b, "\\\""); break;
                case '\\': buf_append(b, "\\\\"); break;
                case '\n': buf_append(b, "\\n");  break;
                case '\t': buf_append(b, "\\t");  break;
                case '\r': buf_append(b, "\\r");  break;
                case '\b': buf_append(b, "\\b");  break;
                case '\f': buf_append(b, "\\f");  break;
                default:   buf_append_char(b, *c); break;
            }
        }
    }
    buf_append_char(b, '"');
}

/* Forward declaration for recursive serialization. */
static void serialize_node(Buffer *b, JsonNode *node, int depth);

static void serialize_node(Buffer *b, JsonNode *node, int depth)
{
    if (!node) {
        buf_append(b, "null");
        return;
    }

    switch (node->type) {
        case JSON_NULL:
            buf_append(b, "null");
            break;

        case JSON_BOOL:
            buf_append(b, node->boolean ? "true" : "false");
            break;

        case JSON_NUMBER: {
            char tmp[64];
            /* Use %g for compact output; switch to %.1f when the value
               is an integer so we always keep at least one decimal. */
            if (node->number == (long long)node->number &&
                node->number >= -1e15 && node->number <= 1e15) {
                snprintf(tmp, sizeof(tmp), "%.1f", node->number);
            } else {
                snprintf(tmp, sizeof(tmp), "%g", node->number);
            }
            buf_append(b, tmp);
            break;
        }

        case JSON_STRING:
            buf_append_escaped(b, node->string);
            break;

        case JSON_ARRAY: {
            if (!node->child) {
                buf_append(b, "[]");
                break;
            }
            buf_append(b, "[\n");
            JsonNode *elem = node->child;
            while (elem) {
                buf_indent(b, depth + 1);
                serialize_node(b, elem, depth + 1);
                if (elem->next) buf_append(b, ",");
                buf_append(b, "\n");
                elem = elem->next;
            }
            buf_indent(b, depth);
            buf_append_char(b, ']');
            break;
        }

        case JSON_OBJECT: {
            if (!node->child) {
                buf_append(b, "{}");
                break;
            }
            buf_append(b, "{\n");
            JsonNode *member = node->child;
            while (member) {
                buf_indent(b, depth + 1);
                buf_append_escaped(b, member->key);
                buf_append(b, ": ");
                serialize_node(b, member, depth + 1);
                if (member->next) buf_append(b, ",");
                buf_append(b, "\n");
                member = member->next;
            }
            buf_indent(b, depth);
            buf_append_char(b, '}');
            break;
        }
    }
}

/* ================================================================== */
/*  PUBLIC API                                                         */
/* ================================================================== */

/*
 * json_parse — Parse a JSON text and return the root node.
 */
JsonNode *json_parse(const char *text)
{
    if (!text) return NULL;
    const char *p = text;
    return parse_value(&p);
}

/*
 * json_parse_file — Read an entire file into memory and parse it.
 */
JsonNode *json_parse_file(const char *filename)
{
    if (!filename) return NULL;

    FILE *fp = fopen(filename, "r");
    if (!fp) return NULL;

    /* Determine file size. */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    if (size <= 0) {
        fclose(fp);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)size, fp);
    buf[nread] = '\0';
    fclose(fp);

    JsonNode *root = json_parse(buf);
    free(buf);
    return root;
}

/*
 * json_get — Find a direct child of an object node by key name.
 */
JsonNode *json_get(JsonNode *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;

    for (JsonNode *c = obj->child; c; c = c->next) {
        if (c->key && strcmp(c->key, key) == 0) {
            return c;
        }
    }
    return NULL;
}

/*
 * json_count — Return the number of children in an array or object.
 */
int json_count(JsonNode *node)
{
    if (!node) return 0;
    int count = 0;
    for (JsonNode *c = node->child; c; c = c->next) {
        count++;
    }
    return count;
}

/*
 * json_index — Return the idx-th child (0-based) of an array or object.
 */
JsonNode *json_index(JsonNode *arr, int idx)
{
    if (!arr || idx < 0) return NULL;
    JsonNode *c = arr->child;
    for (int i = 0; i < idx && c; i++) {
        c = c->next;
    }
    return c;
}

/*
 * json_number — Convenience accessor for a number node's value.
 */
double json_number(JsonNode *node)
{
    return node ? node->number : 0.0;
}

/*
 * json_string — Convenience accessor for a string node's value.
 */
const char *json_string(JsonNode *node)
{
    return node ? node->string : NULL;
}

/*
 * json_bool — Convenience accessor for a boolean node's value.
 */
int json_bool(JsonNode *node)
{
    return node ? node->boolean : 0;
}

/*
 * json_set_number — Set a node's numeric value.
 */
void json_set_number(JsonNode *node, double val)
{
    if (!node) return;
    node->type   = JSON_NUMBER;
    node->number = val;
}

/*
 * json_set_bool — Set a node's boolean value.
 */
void json_set_bool(JsonNode *node, int val)
{
    if (!node) return;
    node->type    = JSON_BOOL;
    node->boolean = val;
}

/*
 * json_set_string — Set a node's string value (frees old string).
 */
void json_set_string(JsonNode *node, const char *val)
{
    if (!node) return;
    node->type = JSON_STRING;
    free(node->string);
    node->string = val ? strdup(val) : NULL;
}

/*
 * json_serialize — Produce a pretty-printed JSON string (caller must free).
 */
char *json_serialize(JsonNode *node)
{
    Buffer b;
    buf_init(&b);
    serialize_node(&b, node, 0);
    buf_append(&b, "\n");   /* trailing newline */
    return b.data;
}

/*
 * json_write_file — Serialize the tree and write it to a file.
 */
int json_write_file(const char *filename, JsonNode *node)
{
    if (!filename) return -1;

    char *text = json_serialize(node);
    if (!text) return -1;

    FILE *fp = fopen(filename, "w");
    if (!fp) {
        free(text);
        return -1;
    }

    fputs(text, fp);
    fclose(fp);
    free(text);
    return 0;
}

/*
 * json_free — Recursively free a JSON tree and all associated memory.
 */
void json_free(JsonNode *node)
{
    if (!node) return;

    /* Free the linked-list siblings via iteration to avoid deep recursion. */
    JsonNode *current = node;
    while (current) {
        JsonNode *next = current->next;

        /* Recursively free children (depth, not breadth). */
        json_free(current->child);

        free(current->key);
        free(current->string);
        free(current);

        current = next;
    }
}
