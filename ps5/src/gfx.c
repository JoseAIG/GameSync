/*
 * GameSync PS5 client - CPU drawing on a 1920x1080 canvas.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Rectangles and anti-aliased text from the baked Vegur atlas (font_data.c).
 * The canvas is plain memory; platform.c hands it to VideoOut each frame.
 */
#include "gfx.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__PROSPERO__) || defined(__unix__)
#include <sys/mman.h>
#endif

static uint32_t *canvas;
/* unpacked 8-bit coverage, built once from the 4-bit atlas */
static uint8_t *atlas;

static inline uint32_t pack(uint32_t rgb)
{
    return ((rgb >> 16) & 0xFF) | (rgb & 0xFF00) | ((rgb & 0xFF) << 16) | 0xFF000000u;
}

bool gfx_init(void)
{
    size_t bytes = (size_t)GFX_W * GFX_ROWS * 4;
#if defined(__PROSPERO__) || defined(__unix__)
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    canvas = p == MAP_FAILED ? NULL : p;
#else
    canvas = calloc(1, bytes);
#endif
    atlas = malloc((size_t)FONT_ATLAS_W * FONT_ATLAS_H);
    if (!canvas || !atlas)
        return false;
    memset(canvas, 0, bytes);
    for (size_t i = 0; i < (size_t)FONT_ATLAS_W * FONT_ATLAS_H; ++i)
    {
        uint8_t b = g_font_atlas4[i >> 1];
        atlas[i] = (uint8_t)(((i & 1) ? b >> 4 : b & 15) * 17);
    }
    return true;
}

uint32_t *gfx_pixels(void)
{
    return canvas;
}

void gfx_clear(uint32_t rgb)
{
    uint32_t v = pack(rgb);
    for (size_t i = 0; i < (size_t)GFX_W * GFX_H; ++i)
        canvas[i] = v;
}

static bool clip(int *x, int *y, int *w, int *h)
{
    if (*x < 0)
    {
        *w += *x;
        *x = 0;
    }
    if (*y < 0)
    {
        *h += *y;
        *y = 0;
    }
    if (*x + *w > GFX_W)
        *w = GFX_W - *x;
    if (*y + *h > GFX_H)
        *h = GFX_H - *y;
    return *w > 0 && *h > 0;
}

void gfx_rect(int x, int y, int w, int h, uint32_t rgb)
{
    if (!clip(&x, &y, &w, &h))
        return;
    uint32_t v = pack(rgb);
    for (int row = 0; row < h; ++row)
    {
        uint32_t *p = canvas + (size_t)(y + row) * GFX_W + x;
        for (int col = 0; col < w; ++col)
            p[col] = v;
    }
}

static inline uint32_t blend(uint32_t dst, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    int dr = (int)(dst & 0xFF), dg = (int)((dst >> 8) & 0xFF), db = (int)((dst >> 16) & 0xFF);
    int ia = (int)a;
    dr += ((int)r - dr) * ia / 255;
    dg += ((int)g - dg) * ia / 255;
    db += ((int)b - db) * ia / 255;
    return (uint32_t)dr | (uint32_t)dg << 8 | (uint32_t)db << 16 | 0xFF000000u;
}

void gfx_rect_blend(int x, int y, int w, int h, uint32_t rgb, uint8_t alpha)
{
    if (!clip(&x, &y, &w, &h))
        return;
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    for (int row = 0; row < h; ++row)
    {
        uint32_t *p = canvas + (size_t)(y + row) * GFX_W + x;
        for (int col = 0; col < w; ++col)
            p[col] = blend(p[col], r, g, b, alpha);
    }
}

void gfx_frame(int x, int y, int w, int h, int t, uint32_t rgb)
{
    gfx_rect(x, y, w, t, rgb);
    gfx_rect(x, y + h - t, w, t, rgb);
    gfx_rect(x, y, t, h, rgb);
    gfx_rect(x + w - t, y, t, h, rgb);
}

void gfx_round_rect(int x, int y, int w, int h, int radius, uint32_t rgb)
{
    if (radius * 2 > h)
        radius = h / 2;
    if (radius * 2 > w)
        radius = w / 2;
    gfx_rect(x, y + radius, w, h - 2 * radius, rgb);
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    for (int i = 0; i < radius; ++i)
    {
        /* distance from the corner circle's centre, with a 1 px soft edge */
        float dy = (float)radius - (float)i - 0.5f;
        for (int j = 0; j < radius; ++j)
        {
            float dx = (float)radius - (float)j - 0.5f;
            float d = dx * dx + dy * dy;
            float rr = (float)radius;
            float a = d <= (rr - 1) * (rr - 1) ? 1.0f : d >= rr * rr ? 0.0f : (rr * rr - d) / (2 * rr - 1);
            if (a <= 0)
                continue;
            uint32_t alpha = (uint32_t)(a * 255);
            int px[2] = {x + j, x + w - 1 - j};
            int py[2] = {y + i, y + h - 1 - i};
            for (int k = 0; k < 4; ++k)
            {
                int cx = px[k & 1], cy = py[k >> 1];
                if (cx < 0 || cy < 0 || cx >= GFX_W || cy >= GFX_H)
                    continue;
                uint32_t *p = canvas + (size_t)cy * GFX_W + cx;
                *p = blend(*p, r, g, b, alpha);
            }
        }
        gfx_rect(x + radius, y + i, w - 2 * radius, 1, rgb);
        gfx_rect(x + radius, y + h - 1 - i, w - 2 * radius, 1, rgb);
    }
}

/* Coverage from a signed distance (negative inside), 1 px soft edge. */
static void plot(int x, int y, float dist, uint32_t rgb)
{
    if (x < 0 || y < 0 || x >= GFX_W || y >= GFX_H || dist >= 0.5f)
        return;
    float a = dist <= -0.5f ? 1.0f : 0.5f - dist;
    uint32_t *p = canvas + (size_t)y * GFX_W + x;
    *p = blend(*p, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, (uint32_t)(a * 255));
}

void gfx_disc(float cx, float cy, float r, uint32_t rgb)
{
    for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); ++y)
        for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); ++x)
        {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            plot(x, y, sqrtf(dx * dx + dy * dy) - r, rgb);
        }
}

void gfx_ring(float cx, float cy, float r, float t, uint32_t rgb)
{
    for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); ++y)
        for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); ++x)
        {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            plot(x, y, fabsf(d - (r - t / 2)) - t / 2, rgb);
        }
}

void gfx_line(float x0, float y0, float x1, float y1, float t, uint32_t rgb)
{
    float minx = fminf(x0, x1) - t, maxx = fmaxf(x0, x1) + t;
    float miny = fminf(y0, y1) - t, maxy = fmaxf(y0, y1) + t;
    float vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
    for (int y = (int)miny; y <= (int)maxy; ++y)
        for (int x = (int)minx; x <= (int)maxx; ++x)
        {
            float px = (float)x + 0.5f - x0, py = (float)y + 0.5f - y0;
            float k = len2 > 0 ? (px * vx + py * vy) / len2 : 0;
            k = k < 0 ? 0 : k > 1 ? 1 : k;
            float dx = px - k * vx, dy = py - k * vy;
            plot(x, y, sqrtf(dx * dx + dy * dy) - t / 2, rgb);
        }
}

/* ------------------------------------------------------------ text */

static const FontGlyph *glyph(FontId font, unsigned char c)
{
    if (c < FONT_FIRST_CHAR || c >= FONT_FIRST_CHAR + FONT_GLYPH_COUNT)
        c = '?';
    return &g_font_faces[font].glyphs[c - FONT_FIRST_CHAR];
}

int gfx_line_height(FontId font)
{
    return g_font_faces[font].ascent + g_font_faces[font].descent;
}

int gfx_ascent(FontId font)
{
    return g_font_faces[font].ascent;
}

/* UTF-8 isn't in the atlas: each multi-byte sequence draws as one '?'. */
static const char *next_char(const char *s, unsigned char *c)
{
    unsigned char b = (unsigned char)*s++;
    if (b < 0x80)
    {
        *c = b;
        return s;
    }
    while ((*s & 0xC0) == 0x80)
        s++;
    *c = '?';
    return s;
}

int gfx_text_width(FontId font, const char *text)
{
    int q = 0;
    unsigned char c;
    while (*text)
    {
        text = next_char(text, &c);
        q += glyph(font, c)->adv_q;
    }
    return (q + 3) / 4;
}

static void draw_glyph(int px, int baseline, const FontGlyph *g, uint32_t r, uint32_t gg,
                       uint32_t b)
{
    int x0 = px + g->xoff, y0 = baseline + g->yoff;
    for (int row = 0; row < g->h; ++row)
    {
        int y = y0 + row;
        if (y < 0 || y >= GFX_H)
            continue;
        const uint8_t *src = atlas + (size_t)(g->y + row) * FONT_ATLAS_W + g->x;
        uint32_t *dst = canvas + (size_t)y * GFX_W;
        for (int col = 0; col < g->w; ++col)
        {
            int x = x0 + col;
            uint32_t a = src[col];
            if (!a || x < 0 || x >= GFX_W)
                continue;
            dst[x] = a == 255 ? (r | gg << 8 | b << 16 | 0xFF000000u) : blend(dst[x], r, gg, b, a);
        }
    }
}

int gfx_text(int x, int y, FontId font, uint32_t rgb, GfxAlign align, const char *text)
{
    int w = gfx_text_width(font, text);
    if (align == ALIGN_CENTER)
        x -= w / 2;
    else if (align == ALIGN_RIGHT)
        x -= w;
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    int baseline = y + g_font_faces[font].ascent;
    int pen_q = x * 4;
    unsigned char c;
    while (*text)
    {
        text = next_char(text, &c);
        const FontGlyph *gl = glyph(font, c);
        if (gl->w)
            draw_glyph((pen_q + 2) / 4, baseline, gl, r, g, b);
        pen_q += gl->adv_q;
    }
    return w;
}

int gfx_text_fit(int x, int y, FontId font, uint32_t rgb, GfxAlign align, int max_w,
                 const char *text)
{
    if (gfx_text_width(font, text) <= max_w)
        return gfx_text(x, y, font, rgb, align, text);
    char buf[512];
    size_t len = strlen(text);
    if (len >= sizeof(buf) - 4)
        len = sizeof(buf) - 4;
    memcpy(buf, text, len);
    while (len > 0)
    {
        /* back off a whole UTF-8 sequence */
        do
            len--;
        while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80);
        memcpy(buf + len, "...", 4);
        if (gfx_text_width(font, buf) <= max_w)
            break;
    }
    return gfx_text(x, y, font, rgb, align, buf);
}

int gfx_textf(int x, int y, FontId font, uint32_t rgb, GfxAlign align, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return gfx_text(x, y, font, rgb, align, buf);
}
