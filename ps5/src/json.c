/*
 * GameSync PS5 client - a small JSON reader and a string builder for writing.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Recursive descent over the whole document into a malloc'd tree. The server's
 * responses are at most a few MB (a ROM catalog page), so a tree is simpler
 * than streaming and fast enough.
 */
#include "json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 64

typedef struct
{
    const char *p;
    int depth;
} Parser;

static void skip_ws(Parser *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')
        ps->p++;
}

static JsonValue *new_value(JsonType type)
{
    JsonValue *v = calloc(1, sizeof(*v));
    if (v)
        v->type = type;
    return v;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static unsigned read_hex4(const char *s, bool *ok)
{
    unsigned v = 0;
    for (int i = 0; i < 4; ++i)
    {
        int h = hex_val(s[i]);
        if (h < 0)
        {
            *ok = false;
            return 0;
        }
        v = v << 4 | (unsigned)h;
    }
    return v;
}

static size_t put_utf8(char *out, unsigned cp)
{
    if (cp < 0x80)
    {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800)
    {
        out[0] = (char)(0xC0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000)
    {
        out[0] = (char)(0xE0 | cp >> 12);
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | cp >> 18);
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* ps->p at the opening quote. */
static char *parse_string(Parser *ps)
{
    const char *s = ++ps->p;
    size_t raw = 0;
    while (s[raw] && s[raw] != '"')
    {
        if (s[raw] == '\\' && s[raw + 1])
            raw++;
        raw++;
    }
    if (s[raw] != '"')
        return NULL;
    /* unescaped is never longer than the raw text (\uXXXX -> at most 4 bytes, 6 raw) */
    char *out = malloc(raw + 1);
    if (!out)
        return NULL;
    size_t o = 0;
    const char *p = s;
    while (*p != '"')
    {
        if (*p != '\\')
        {
            out[o++] = *p++;
            continue;
        }
        p++;
        switch (*p)
        {
        case '"': out[o++] = '"'; break;
        case '\\': out[o++] = '\\'; break;
        case '/': out[o++] = '/'; break;
        case 'b': out[o++] = '\b'; break;
        case 'f': out[o++] = '\f'; break;
        case 'n': out[o++] = '\n'; break;
        case 'r': out[o++] = '\r'; break;
        case 't': out[o++] = '\t'; break;
        case 'u':
        {
            bool ok = true;
            unsigned cp = read_hex4(p + 1, &ok);
            if (!ok)
            {
                free(out);
                return NULL;
            }
            p += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF && p[1] == '\\' && p[2] == 'u')
            {
                unsigned lo = read_hex4(p + 3, &ok);
                if (ok && lo >= 0xDC00 && lo <= 0xDFFF)
                {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    p += 6;
                }
            }
            o += put_utf8(out + o, cp);
            break;
        }
        default:
            free(out);
            return NULL;
        }
        p++;
    }
    out[o] = 0;
    ps->p = p + 1;
    return out;
}

static JsonValue *parse_value(Parser *ps);

static JsonValue *parse_container(Parser *ps, bool object)
{
    if (++ps->depth > MAX_DEPTH)
        return NULL;
    JsonValue *v = new_value(object ? JSON_OBJECT : JSON_ARRAY);
    if (!v)
        return NULL;
    ps->p++;
    skip_ws(ps);
    char close = object ? '}' : ']';
    if (*ps->p == close)
    {
        ps->p++;
        ps->depth--;
        return v;
    }
    JsonValue **tail = &v->child;
    for (;;)
    {
        char *key = NULL;
        skip_ws(ps);
        if (object)
        {
            if (*ps->p != '"' || !(key = parse_string(ps)))
                goto fail;
            skip_ws(ps);
            if (*ps->p != ':')
            {
                free(key);
                goto fail;
            }
            ps->p++;
        }
        JsonValue *item = parse_value(ps);
        if (!item)
        {
            free(key);
            goto fail;
        }
        item->key = key;
        *tail = item;
        tail = &item->next;
        skip_ws(ps);
        if (*ps->p == ',')
        {
            ps->p++;
            continue;
        }
        if (*ps->p == close)
        {
            ps->p++;
            ps->depth--;
            return v;
        }
        goto fail;
    }
fail:
    json_free(v);
    return NULL;
}

static JsonValue *parse_value(Parser *ps)
{
    skip_ws(ps);
    char c = *ps->p;
    if (c == '{')
        return parse_container(ps, true);
    if (c == '[')
        return parse_container(ps, false);
    if (c == '"')
    {
        char *s = parse_string(ps);
        if (!s)
            return NULL;
        JsonValue *v = new_value(JSON_STRING);
        if (!v)
        {
            free(s);
            return NULL;
        }
        v->str = s;
        return v;
    }
    if (strncmp(ps->p, "true", 4) == 0 || strncmp(ps->p, "false", 5) == 0)
    {
        JsonValue *v = new_value(JSON_BOOL);
        if (!v)
            return NULL;
        v->boolean = c == 't';
        ps->p += v->boolean ? 4 : 5;
        return v;
    }
    if (strncmp(ps->p, "null", 4) == 0)
    {
        ps->p += 4;
        return new_value(JSON_NULL);
    }
    if (c == '-' || (c >= '0' && c <= '9'))
    {
        char *end;
        double d = strtod(ps->p, &end);
        if (end == ps->p)
            return NULL;
        ps->p = end;
        JsonValue *v = new_value(JSON_NUMBER);
        if (v)
            v->num = d;
        return v;
    }
    return NULL;
}

JsonValue *json_parse(const char *text)
{
    if (!text)
        return NULL;
    Parser ps = {text, 0};
    JsonValue *v = parse_value(&ps);
    if (!v)
        return NULL;
    skip_ws(&ps);
    if (*ps.p)
    {
        json_free(v);
        return NULL;
    }
    return v;
}

void json_free(JsonValue *value)
{
    while (value)
    {
        JsonValue *next = value->next;
        json_free(value->child);
        free(value->key);
        free(value->str);
        free(value);
        value = next;
    }
}

JsonValue *json_get(const JsonValue *object, const char *key)
{
    if (!object || object->type != JSON_OBJECT)
        return NULL;
    for (JsonValue *m = object->child; m; m = m->next)
        if (m->key && strcmp(m->key, key) == 0)
            return m;
    return NULL;
}

const char *json_get_str(const JsonValue *object, const char *key, const char *fallback)
{
    JsonValue *v = json_get(object, key);
    return v && v->type == JSON_STRING ? v->str : fallback;
}

double json_get_num(const JsonValue *object, const char *key, double fallback)
{
    JsonValue *v = json_get(object, key);
    return v && v->type == JSON_NUMBER ? v->num : fallback;
}

bool json_get_bool(const JsonValue *object, const char *key, bool fallback)
{
    JsonValue *v = json_get(object, key);
    return v && v->type == JSON_BOOL ? v->boolean : fallback;
}

int json_count(const JsonValue *array)
{
    int n = 0;
    if (array)
        for (JsonValue *m = array->child; m; m = m->next)
            n++;
    return n;
}

/* ------------------------------------------------------------ StrBuf */

void sb_init(StrBuf *sb)
{
    memset(sb, 0, sizeof(*sb));
}

void sb_free(StrBuf *sb)
{
    free(sb->data);
    sb_init(sb);
}

static bool sb_reserve(StrBuf *sb, size_t extra)
{
    if (sb->failed)
        return false;
    if (sb->len + extra + 1 <= sb->cap)
        return true;
    size_t cap = sb->cap ? sb->cap : 256;
    while (cap < sb->len + extra + 1)
        cap *= 2;
    char *d = realloc(sb->data, cap);
    if (!d)
    {
        sb->failed = true;
        return false;
    }
    sb->data = d;
    sb->cap = cap;
    return true;
}

void sb_append(StrBuf *sb, const char *s)
{
    size_t n = strlen(s);
    if (!sb_reserve(sb, n))
        return;
    memcpy(sb->data + sb->len, s, n + 1);
    sb->len += n;
}

void sb_appendf(StrBuf *sb, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char small[256];
    int n = vsnprintf(small, sizeof(small), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n < sizeof(small))
    {
        sb_append(sb, small);
        return;
    }
    if (!sb_reserve(sb, (size_t)n))
        return;
    va_start(ap, fmt);
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    sb->len += (size_t)n;
}

void sb_append_json_str(StrBuf *sb, const char *s)
{
    sb_append(sb, "\"");
    for (; *s; ++s)
    {
        unsigned char c = (unsigned char)*s;
        char esc[8];
        switch (c)
        {
        case '"': sb_append(sb, "\\\""); break;
        case '\\': sb_append(sb, "\\\\"); break;
        case '\n': sb_append(sb, "\\n"); break;
        case '\r': sb_append(sb, "\\r"); break;
        case '\t': sb_append(sb, "\\t"); break;
        default:
            if (c < 0x20)
            {
                snprintf(esc, sizeof(esc), "\\u%04x", c);
                sb_append(sb, esc);
            }
            else
            {
                esc[0] = (char)c;
                esc[1] = 0;
                sb_append(sb, esc);
            }
        }
    }
    sb_append(sb, "\"");
}
