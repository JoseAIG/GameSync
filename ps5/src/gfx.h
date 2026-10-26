/*
 * GameSync PS5 client - CPU drawing on a 1920x1080 canvas.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_GFX_H
#define GAMESYNC_GFX_H

#include "font_data.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GFX_W 1920
#define GFX_H 1080
/* rows allocated: whole 128-row tile blocks, so the presenter reads 9 full ones */
#define GFX_ROWS 1152

typedef enum
{
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT,
} GfxAlign;

bool gfx_init(void);
/* R,G,B,A byte order, GFX_W pixels a row, GFX_ROWS rows. */
uint32_t *gfx_pixels(void);

/* Colours are 0xRRGGBB. */
void gfx_clear(uint32_t rgb);
void gfx_rect(int x, int y, int w, int h, uint32_t rgb);
void gfx_rect_blend(int x, int y, int w, int h, uint32_t rgb, uint8_t alpha);
void gfx_frame(int x, int y, int w, int h, int thickness, uint32_t rgb);
/* A rectangle with rounded corners. */
void gfx_round_rect(int x, int y, int w, int h, int radius, uint32_t rgb);
/* Anti-aliased shapes (button glyphs, status dots). */
void gfx_disc(float cx, float cy, float r, uint32_t rgb);
void gfx_ring(float cx, float cy, float r, float thickness, uint32_t rgb);
void gfx_line(float x0, float y0, float x1, float y1, float thickness, uint32_t rgb);

int gfx_line_height(FontId font);
int gfx_ascent(FontId font);
int gfx_text_width(FontId font, const char *text);
/* y is the top of the line. Returns the width drawn. */
int gfx_text(int x, int y, FontId font, uint32_t rgb, GfxAlign align, const char *text);
/* Shortened with "..." to fit max_w. */
int gfx_text_fit(int x, int y, FontId font, uint32_t rgb, GfxAlign align, int max_w,
                 const char *text);
int gfx_textf(int x, int y, FontId font, uint32_t rgb, GfxAlign align, const char *fmt, ...)
    __attribute__((format(printf, 6, 7)));

#endif
