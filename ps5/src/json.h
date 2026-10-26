/*
 * GameSync PS5 client - a small JSON reader and a string builder for writing.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_JSON_H
#define GAMESYNC_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;
struct JsonValue
{
    JsonType type;
    char *key;          /* member name inside an object, else NULL */
    char *str;          /* JSON_STRING (UTF-8, unescaped) */
    double num;         /* JSON_NUMBER */
    bool boolean;       /* JSON_BOOL */
    JsonValue *child;   /* first element / member */
    JsonValue *next;    /* next sibling */
};

/* Parses text into a tree; NULL on a syntax error or no memory. */
JsonValue *json_parse(const char *text);
void json_free(JsonValue *value);

JsonValue *json_get(const JsonValue *object, const char *key);
const char *json_get_str(const JsonValue *object, const char *key, const char *fallback);
double json_get_num(const JsonValue *object, const char *key, double fallback);
bool json_get_bool(const JsonValue *object, const char *key, bool fallback);
int json_count(const JsonValue *array);

/* Growable string for building request bodies. */
typedef struct
{
    char *data;
    size_t len;
    size_t cap;
    bool failed; /* an allocation failed: data holds what fit */
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
void sb_append(StrBuf *sb, const char *s);
void sb_appendf(StrBuf *sb, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Appends s as a quoted, escaped JSON string. */
void sb_append_json_str(StrBuf *sb, const char *s);

#endif
