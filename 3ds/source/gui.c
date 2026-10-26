// Drawing kit of the 3DS client (see gui.h).
//
// Spring response/damping parameterisation, momentum projection and
// rubber-band formulas from Apple's *Designing Fluid Interfaces* (WWDC 2018).
// The springs themselves live in gui_motion.c.
#include "gui.h"
#include "network.h"
#include <3ds/util/utf.h>
#include <math.h>
#include <stdarg.h>

static C3D_RenderTarget *target_top, *target_bottom;
static C2D_TextBuf text_buf;
static float screen_w = GUI_TOP_W;
static int screen_id = GUI_TOP;
static u32 ticks;
static const char *ellipsis = "...";

// Monotonic clock: system ticks since gui_init()
static u64 tick0, frame_tick, frame_ms;
static float frame_dt = 1.0f / 60.0f;

u32 gui_mix_hex(u32 a, u32 b, float t) {
    if (!(t > 0)) t = 0;   // also NaN
    if (t > 1) t = 1;
    int r = (int)(((a >> 16) & 0xFF) * (1 - t) + ((b >> 16) & 0xFF) * t + 0.5f);
    int g = (int)(((a >> 8) & 0xFF) * (1 - t) + ((b >> 8) & 0xFF) * t + 0.5f);
    int bl = (int)((a & 0xFF) * (1 - t) + (b & 0xFF) * t + 0.5f);
    return ((u32)r << 16) | ((u32)g << 8) | (u32)bl;
}

u32 gui_mix(u32 a, u32 b, float t) {
    return gui_rgb(gui_mix_hex(a, b, t));
}

// ---------------------------------------------------------------------------
// Lifetime and frames
// ---------------------------------------------------------------------------

void gui_init(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    target_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    target_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    text_buf = C2D_TextBufNew(6144);
    fontEnsureMapped();
    tick0 = frame_tick = svcGetSystemTick();

    // Use a real ellipsis when the system font has one
    CFNT_s *font = fontGetSystemFont();
    if (font && fontGlyphIndexFromCodePoint(font, 0x2026) != fontGetInfo(font)->alterCharIndex)
        ellipsis = "\xE2\x80\xA6";
}

void gui_exit(void) {
    C2D_TextBufDelete(text_buf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

u64 gui_clock_ms(void) {
    return (svcGetSystemTick() - tick0) / (SYSCLOCK_ARM11 / 1000);
}

void gui_begin(bool vsync) {
    C3D_FrameBegin(vsync ? C3D_FRAME_SYNCDRAW : 0);
    C2D_TextBufClear(text_buf);
    ticks++;
    // Sampled after the vsync wait, so dt is the real frame-to-frame time
    u64 now = svcGetSystemTick();
    frame_dt = gui_dt_clamp((float)(now - frame_tick) / (float)SYSCLOCK_ARM11);
    frame_tick = now;
    frame_ms = (now - tick0) / (SYSCLOCK_ARM11 / 1000);
}

float gui_dt(void) { return frame_dt; }
u64 gui_now_ms(void) { return frame_ms; }

void gui_screen(int screen) {
    C3D_RenderTarget *t = (screen == GUI_TOP) ? target_top : target_bottom;
    screen_id = screen;
    screen_w = (screen == GUI_TOP) ? GUI_TOP_W : GUI_BOT_W;
    C2D_TargetClear(t, gui_rgb(HEX_BG));
    C2D_SceneBegin(t);
    // A clip left on by mistake must not hide the next screen
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    // Soft vertical gradient behind everything
    gui_vgrad(0, 0, screen_w, GUI_H, gui_rgb(0x131D29), gui_rgb(HEX_BG));
}

#ifdef GUI_PERF
u32 gui_perf_coast_overspeed;

// Top-right corner of the top screen: the previous frame's citro3d time
// (command building + GPU drawing), red past the 12 ms budget
static void perf_overlay(void) {
    float ms = C3D_GetProcessingTime() + C3D_GetDrawingTime();
    char buf[48];
    if (gui_perf_coast_overspeed)
        snprintf(buf, sizeof(buf), "%.1f ms  coast>v %lu", ms, (unsigned long)gui_perf_coast_overspeed);
    else
        snprintf(buf, sizeof(buf), "%.1f ms", ms);
    C2D_SceneBegin(target_top);
    float w = gui_text_w(GUI_S_SMALL, buf) + 8;
    gui_rect(GUI_TOP_W - w - 2, 2, w, 14, gui_rgba(0x000000, 0xC0));
    gui_text(GUI_TOP_W - 6, 1, GUI_S_SMALL, gui_rgb(ms > 12.0f || gui_perf_coast_overspeed ? HEX_ERR : HEX_OK), GUI_RIGHT, buf);
}
#endif

void gui_end(void) {
#ifdef GUI_PERF
    perf_overlay();
#endif
    C3D_FrameEnd(0);
}

float gui_w(void) { return screen_w; }
u32 gui_ticks(void) { return ticks; }

void gui_transition_start(GuiTransition *t, float from_px) {
    t->from = from_px;
    t->start_ms = gui_clock_ms();
    t->frame = ticks;
    gui_spring_set(&t->off, gui_reduce_motion() ? 0 : from_px);
}

void gui_transition_begin(GuiTransition *t) {
    float fade;
    if (gui_reduce_motion()) {
        // Opacity only, timed: GUI_FADE_MS from fully faded to clear
        gui_spring_set(&t->off, 0);
        u64 age = gui_now_ms() > t->start_ms ? gui_now_ms() - t->start_ms : 0;
        fade = age >= GUI_FADE_MS ? 0 : 0.6f * (1.0f - (float)age / GUI_FADE_MS);
    } else {
        if (t->frame != ticks) {
            t->frame = ticks;
            gui_spring(&t->off, 0, GUI_RESP_PUSH, frame_dt);
        }
        float k = t->from != 0 ? fabsf(t->off.x) / fabsf(t->from) : 0;
        fade = 0.6f * (k > 1 ? 1 : k);
    }
    if (t->off.x != 0) C2D_ViewTranslate(t->off.x, 0);
    // citro2d has no global alpha: tint toward the background instead
    if (fade > 0) C2D_Fade(gui_rgba(HEX_BG, (u8)(fade * 255)));
}

void gui_transition_end(void) {
    C2D_ViewReset();
    C2D_Fade(0);
}

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------

void gui_rect(float x, float y, float w, float h, u32 color) {
    if (w <= 0 || h <= 0) return;
    C2D_DrawRectSolid(x, y, 0.5f, w, h, color);
}

void gui_vgrad(float x, float y, float w, float h, u32 top, u32 bottom) {
    C2D_DrawRectangle(x, y, 0.5f, w, h, top, top, bottom, bottom);
}

void gui_circle(float cx, float cy, float r, u32 color) {
    C2D_DrawCircleSolid(cx, cy, 0.5f, r, color);
}

void gui_rrect(float x, float y, float w, float h, float r, u32 color) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    if (r < 1) {
        gui_rect(x, y, w, h, color);
        return;
    }
    gui_rect(x + r, y, w - 2 * r, h, color);
    gui_rect(x, y + r, r, h - 2 * r, color);
    gui_rect(x + w - r, y + r, r, h - 2 * r, color);
    gui_circle(x + r, y + r, r, color);
    gui_circle(x + w - r, y + r, r, color);
    gui_circle(x + r, y + h - r, r, color);
    gui_circle(x + w - r, y + h - r, r, color);
}

void gui_icon_up(float cx, float cy, float s, u32 color) {
    float h = s / 2;
    C2D_DrawTriangle(cx, cy - h, color, cx - h, cy, color, cx + h, cy, color, 0.5f);
    gui_rect(cx - s * 0.17f, cy - 0.5f, s * 0.34f, h + 0.5f, color);
}

void gui_icon_down(float cx, float cy, float s, u32 color) {
    float h = s / 2;
    C2D_DrawTriangle(cx, cy + h, color, cx - h, cy, color, cx + h, cy, color, 0.5f);
    gui_rect(cx - s * 0.17f, cy - h, s * 0.34f, h + 0.5f, color);
}

void gui_icon_check(float cx, float cy, float s, u32 color) {
    float t = s * 0.18f;
    if (t < 1.5f) t = 1.5f;
    C2D_DrawLine(cx - s * 0.42f, cy, color, cx - s * 0.12f, cy + s * 0.3f, color, t, 0.5f);
    C2D_DrawLine(cx - s * 0.12f, cy + s * 0.3f, color, cx + s * 0.45f, cy - s * 0.32f, color, t, 0.5f);
}

static float icon_stroke(float s) {
    float t = s * 0.18f;
    return t < 1.5f ? 1.5f : t;
}

void gui_icon_x(float cx, float cy, float s, u32 color) {
    float h = s * 0.35f, t = icon_stroke(s);
    C2D_DrawLine(cx - h, cy - h, color, cx + h, cy + h, color, t, 0.5f);
    C2D_DrawLine(cx - h, cy + h, color, cx + h, cy - h, color, t, 0.5f);
}

void gui_icon_chevron_left(float cx, float cy, float s, u32 color) {
    float t = icon_stroke(s);
    C2D_DrawLine(cx + s * 0.22f, cy - s * 0.42f, color, cx - s * 0.22f, cy, color, t, 0.5f);
    C2D_DrawLine(cx - s * 0.22f, cy, color, cx + s * 0.22f, cy + s * 0.42f, color, t, 0.5f);
}

void gui_icon_search(float cx, float cy, float s, u32 color) {
    // Ring of short segments (citro2d has no ring) and a handle at 45 degrees
    float t = icon_stroke(s) * 0.8f, r = s * 0.3f;
    float rx = cx - s * 0.1f, ry = cy - s * 0.1f;
    const int n = 12;
    for (int i = 0; i < n; i++) {
        float a0 = (float)i / n * 2 * (float)M_PI, a1 = (float)(i + 1) / n * 2 * (float)M_PI;
        C2D_DrawLine(rx + cosf(a0) * r, ry + sinf(a0) * r, color, rx + cosf(a1) * r, ry + sinf(a1) * r, color,
                     t, 0.5f);
    }
    float d = r * 0.7071f;
    C2D_DrawLine(rx + d, ry + d, color, cx + s * 0.42f, cy + s * 0.42f, color, t * 1.3f, 0.5f);
}

// The screens are rotated 90 degrees in the framebuffer: x on screen runs
// down the framebuffer from its far end, y on screen runs across it from
// its far end. Coordinates are clamped to the screen.
void gui_clip(float x, float y, float w, float h) {
    C2D_Flush();
    float W = screen_w;
    float left = GUI_H - (y + h), top = W - (x + w), right = GUI_H - y, bottom = W - x;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > GUI_H) right = GUI_H;
    if (bottom > W) bottom = W;
    if (right < left) right = left;
    if (bottom < top) bottom = top;
    C3D_SetScissor(GPU_SCISSOR_NORMAL, (u32)left, (u32)top, (u32)right, (u32)bottom);
}

void gui_clip_off(void) {
    C2D_Flush();
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

float gui_line_h(float scale) {
    return fontGetInfo(NULL)->lineFeed * scale;
}

static float glyph_w(u32 cp, float scale) {
    int index = fontGlyphIndexFromCodePoint(NULL, cp);
    charWidthInfo_s *cw = fontGetCharWidthInfo(NULL, index);
    return cw ? cw->charWidth * scale : 0;
}

// Next code point of `p`; returns bytes consumed (>= 1)
static int next_cp(const char *p, u32 *cp) {
    ssize_t n = decode_utf8(cp, (const uint8_t *)p);
    if (n <= 0) {
        *cp = '?';
        return 1;
    }
    return (int)n;
}

float gui_text_w(float scale, const char *s) {
    float w = 0;
    if (!s) return 0;
    while (*s && *s != '\n') {
        u32 cp;
        s += next_cp(s, &cp);
        w += glyph_w(cp, scale);
    }
    return w;
}

float gui_text(float x, float y, float scale, u32 color, GuiAlign align, const char *s) {
    if (!s || !*s) return 0;
    C2D_Text t;
    C2D_TextParse(&t, text_buf, s);
    C2D_TextOptimize(&t);
    u32 flags = C2D_WithColor;
    if (align == GUI_CENTER) flags |= C2D_AlignCenter;
    else if (align == GUI_RIGHT) flags |= C2D_AlignRight;
    C2D_DrawText(&t, flags, x, y, 0.5f, scale, scale, color);
    return t.width * scale;
}

float gui_textf(float x, float y, float scale, u32 color, GuiAlign align, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return gui_text(x, y, scale, color, align, buf);
}

// Copy the longest prefix of `s` that fits `max_w` together with an
// ellipsis; returns true if it had to cut
static bool fit(const char *s, float scale, float max_w, char *out, size_t size) {
    if (gui_text_w(scale, s) <= max_w) {
        snprintf(out, size, "%s", s);
        // A newline ends the text here
        char *nl = strchr(out, '\n');
        if (nl) *nl = '\0';
        return false;
    }
    float limit = max_w - gui_text_w(scale, ellipsis);
    float w = 0;
    const char *p = s;
    while (*p && *p != '\n') {
        u32 cp;
        int n = next_cp(p, &cp);
        float gw = glyph_w(cp, scale);
        if (w + gw > limit) break;
        w += gw;
        p += n;
    }
    size_t len = (size_t)(p - s);
    // Don't leave a trailing space before the ellipsis
    while (len > 0 && s[len - 1] == ' ') len--;
    if (len + strlen(ellipsis) + 1 > size) len = size - strlen(ellipsis) - 1;
    memcpy(out, s, len);
    strcpy(out + len, ellipsis);
    return true;
}

const char *gui_ellipsis(void) { return ellipsis; }

void gui_fit(const char *s, float scale, float max_w, char *out, size_t size) {
    if (!out || size == 0) return;
    if (!s) s = "";
    fit(s, scale, max_w, out, size);
}

float gui_text_fit(float x, float y, float scale, u32 color, GuiAlign align, float max_w, const char *s) {
    if (!s || !*s) return 0;
    char buf[GUI_WRAP_LINE * 2];
    fit(s, scale, max_w, buf, sizeof(buf));
    return gui_text(x, y, scale, color, align, buf);
}

float gui_text_mid(float x, float y, float h, float scale, u32 color, GuiAlign align, float max_w, const char *s) {
    float ty = y + (h - gui_line_h(scale)) / 2 + scale * 1.5f;
    if (max_w > 0) return gui_text_fit(x, ty, scale, color, align, max_w, s);
    return gui_text(x, ty, scale, color, align, s);
}

int gui_wrap(const char *s, float scale, float max_w, char lines[][GUI_WRAP_LINE], int max_lines) {
    int count = 0;
    if (!s || max_lines <= 0) return 0;
    const char *p = s;
    while (*p && count < max_lines) {
        // One line: advance until the width runs out or a newline
        const char *start = p, *cut = NULL, *after_cut = NULL;
        float w = 0;
        while (*p && *p != '\n') {
            u32 cp;
            int n = next_cp(p, &cp);
            float gw = glyph_w(cp, scale);
            if (w + gw > max_w && p > start) break;
            if (cp == ' ') {
                cut = p;
                after_cut = p + n;
            }
            w += gw;
            p += n;
            if ((size_t)(p - start) >= GUI_WRAP_LINE - 4) break;
        }
        const char *end = p;
        if (*p && *p != '\n' && cut && cut > start) {
            end = cut;
            p = after_cut;
        }
        size_t len = (size_t)(end - start);
        if (len > GUI_WRAP_LINE - 1) len = GUI_WRAP_LINE - 1;
        memcpy(lines[count], start, len);
        lines[count][len] = '\0';
        count++;
        if (*p == '\n') p++;
        else while (*p == ' ') p++;
    }
    if (*p && count > 0) {
        // Text left over: ellipsis on the last line
        char tmp[GUI_WRAP_LINE + 8];
        snprintf(tmp, sizeof(tmp), "%s%s", lines[count - 1], ellipsis);
        fit(tmp, scale, max_w, lines[count - 1], GUI_WRAP_LINE);
        if (!strstr(lines[count - 1], ellipsis)) {
            size_t l = strlen(lines[count - 1]);
            if (l + strlen(ellipsis) < GUI_WRAP_LINE) strcat(lines[count - 1], ellipsis);
        }
    }
    return count;
}

int gui_text_wrap(float x, float y, float scale, u32 color, float max_w, int max_lines, const char *s) {
    char lines[8][GUI_WRAP_LINE];
    if (max_lines > 8) max_lines = 8;
    int n = gui_wrap(s, scale, max_w, lines, max_lines);
    float lh = gui_line_h(scale);
    for (int i = 0; i < n; i++) gui_text(x, y + i * lh, scale, color, GUI_LEFT, lines[i]);
    return n;
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

float gui_pill_w(float h, float scale, const char *label) {
    return gui_text_w(scale, label) + h * 0.9f;
}

float gui_pill(float x, float y, float h, float scale, u32 bg, u32 fg, const char *label) {
    float pad = h * 0.45f;
    float w = gui_text_w(scale, label) + pad * 2;
    gui_rrect(x, y, w, h, h / 2, bg);
    gui_text_mid(x + pad, y, h, scale, fg, GUI_LEFT, 0, label);
    return w;
}

float gui_pill_outline(float x, float y, float h, float scale, u32 color, u32 fill, const char *label) {
    float pad = h * 0.45f;
    float w = gui_text_w(scale, label) + pad * 2;
    gui_rrect(x, y, w, h, h / 2, color);
    gui_rrect(x + 1, y + 1, w - 2, h - 2, (h - 2) / 2, fill);
    gui_text_mid(x + pad, y, h, scale, color, GUI_LEFT, 0, label);
    return w;
}

static u32 button_hex(const char *b) {
    if (!strcmp(b, "A")) return 0xE5534B;
    if (!strcmp(b, "B")) return 0xE3B341;
    if (!strcmp(b, "X")) return 0x539BF5;
    if (!strcmp(b, "Y")) return 0x57AB5A;
    return 0xAAB8C5;
}

static bool is_shoulder(const char *b) {
    return !strcmp(b, "L") || !strcmp(b, "R") || !strcmp(b, "ZR");
}

float gui_button_w(const char *b) {
    if (!strcmp(b, "ZR")) return 21;
    if (strlen(b) == 1) return (*b == 'L' || *b == 'R') ? 17 : 13;
    if (!strcmp(b, "DPAD") || !strcmp(b, "UD") || !strcmp(b, "LR")) return 13;
    return gui_text_w(0.32f, b) + 8;
}

// `muted`: the glyph of a disabled button (flat, no key colour)
static float button_glyph(float x, float cy, const char *b, bool muted) {
    u32 ink = gui_rgb(muted ? HEX_MUTED : HEX_INK);
    u32 fill = gui_rgb(muted ? HEX_LINE : button_hex(b));
    if (is_shoulder(b)) {
        // Shoulder button: rounded tab
        float w = gui_button_w(b);
        gui_rrect(x, cy - 6.5f, w, 13, 4, fill);
        gui_text_mid(x + w / 2, cy - 6.5f, 13, 0.38f, ink, GUI_CENTER, 0, b);
        return w;
    }
    if (strlen(b) == 1) {
        gui_circle(x + 6.5f, cy, 6.5f, fill);
        gui_text_mid(x + 6.6f, cy - 6.5f, 13, 0.38f, ink, GUI_CENTER, 0, b);
        return 13;
    }
    if (!strcmp(b, "DPAD") || !strcmp(b, "UD") || !strcmp(b, "LR")) {
        u32 base = gui_rgb(muted ? HEX_LINE : 0x6F8295), hi = gui_rgb(muted ? HEX_MUTED : HEX_TEXT);
        bool ud = !strcmp(b, "UD"), lr = !strcmp(b, "LR"), all = !ud && !lr;
        // Cross: vertical arm, horizontal arm, then highlighted halves
        gui_rect(x + 4.5f, cy - 6.5f, 4, 13, (ud || all) ? hi : base);
        gui_rect(x, cy - 2, 13, 4, (lr || all) ? hi : base);
        if (lr) gui_rect(x + 4.5f, cy - 6.5f, 4, 4.5f, base), gui_rect(x + 4.5f, cy + 2, 4, 4.5f, base);
        if (ud) gui_rect(x, cy - 2, 4.5f, 4, base), gui_rect(x + 8.5f, cy - 2, 4.5f, 4, base);
        return 13;
    }
    // START / SELECT
    float w = gui_text_w(0.32f, b) + 8;
    gui_rrect(x, cy - 5.5f, w, 11, 5.5f, fill);
    gui_text_mid(x + w / 2, cy - 5.5f, 11, 0.32f, ink, GUI_CENTER, 0, b);
    return w;
}

float gui_button(float x, float cy, const char *b) {
    return button_glyph(x, cy, b, false);
}

// A button glyph names a key ("A"), a drawn icon ("^up", "^down",
// "^search") or a key then an icon ("A^up")
enum { BTN_ICON_NONE = 0, BTN_ICON_UP, BTN_ICON_DOWN, BTN_ICON_SEARCH };
#define BTN_ICON_W 10

static void split_glyph(const char *glyph, char *key, size_t key_size, int *icon) {
    *icon = BTN_ICON_NONE;
    key[0] = '\0';
    if (!glyph) return;
    const char *caret = strchr(glyph, '^');
    size_t n = caret ? (size_t)(caret - glyph) : strlen(glyph);
    if (n >= key_size) n = key_size - 1;
    memcpy(key, glyph, n);
    key[n] = '\0';
    if (!caret) return;
    if (!strcmp(caret + 1, "up")) *icon = BTN_ICON_UP;
    else if (!strcmp(caret + 1, "down")) *icon = BTN_ICON_DOWN;
    else if (!strcmp(caret + 1, "search")) *icon = BTN_ICON_SEARCH;
}

static float glyph_part_w(const char *key, int icon) {
    float w = key[0] ? gui_button_w(key) : 0;
    if (icon) w += (w > 0 ? 4 : 0) + BTN_ICON_W;
    return w;
}

float gui_btn_content_w(const char *glyph, const char *label) {
    char key[12];
    int icon;
    split_glyph(glyph, key, sizeof(key), &icon);
    float w = glyph_part_w(key, icon);
    if (label && *label) w += (w > 0 ? 4 : 0) + gui_text_w(GUI_S_SMALL, label);
    return w;
}

void gui_btn(GuiRect r, const char *glyph, const char *label, GuiBtnStyle style, u32 tone_hex, bool pressed) {
    gui_btn_ex(r, glyph, label, style, tone_hex, pressed, false);
}

void gui_btn_ex(GuiRect r, const char *glyph, const char *label, GuiBtnStyle style, u32 tone_hex,
                bool pressed, bool on) {
    if (r.w <= 0 || r.h <= 0) return;
    if (style == GUI_BTN_DISABLED) pressed = false;
    if (!tone_hex) tone_hex = HEX_ACCENT;
    if (pressed) r = gui_rect_grow(r, -1);   // the 0.97-scale equivalent at this size
    u32 text;
    switch (style) {
        case GUI_BTN_PRIMARY:
            gui_rrect(r.x, r.y, r.w, r.h, GUI_BTN_R,
                      pressed ? gui_mix(tone_hex, 0x000000, 0.20f) : gui_rgb(tone_hex));
            text = HEX_INK;
            break;
        case GUI_BTN_TONAL:
            gui_rrect(r.x, r.y, r.w, r.h, GUI_BTN_R, gui_rgb(HEX_LINE));
            gui_rrect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, GUI_BTN_R - 1,
                      gui_mix(HEX_PANEL_HI, tone_hex, on ? (pressed ? 0.55f : 0.35f) : (pressed ? 0.35f : 0)));
            text = HEX_TEXT;
            break;
        default:
            gui_rrect(r.x, r.y, r.w, r.h, GUI_BTN_R, gui_rgb(HEX_BG2));
            text = HEX_MUTED;
            break;
    }
    // Glyph (key, icon) + label centred, never closer than 6 px to the edges
    char key[12];
    int icon;
    split_glyph(glyph, key, sizeof(key), &icon);
    float gw = glyph_part_w(key, icon);
    if (gw > 0) gw += 4;
    char fitted[GUI_WRAP_LINE];
    gui_fit(label ? label : "", GUI_S_SMALL, r.w - 12 - gw, fitted, sizeof(fitted));
    float cw = gw + gui_text_w(GUI_S_SMALL, fitted);
    if (!fitted[0] && gw > 0) cw -= 4;
    float x = r.x + (r.w - cw) / 2;
    if (x < r.x + 6) x = r.x + 6;
    float cy = r.y + r.h / 2;
    if (key[0]) x += button_glyph(x, cy, key, style == GUI_BTN_DISABLED) + 4;
    if (icon) {
        u32 c = gui_rgb(text);
        float icx = x + BTN_ICON_W / 2.0f;
        if (icon == BTN_ICON_UP) gui_icon_up(icx, cy, 9, c);
        else if (icon == BTN_ICON_DOWN) gui_icon_down(icx, cy, 9, c);
        else gui_icon_search(icx, cy, 11, c);
        x += BTN_ICON_W + 4;
    }
    if (fitted[0]) gui_text_mid(x, r.y, r.h, GUI_S_SMALL, gui_rgb(text), GUI_LEFT, 0, fitted);
}

void gui_bar(float x, float y, float w, float h, float frac, u32 color) {
    gui_rrect(x, y, w, h, h / 2, gui_rgb(HEX_BG2));
    if (frac < 0) {
        // Indeterminate: a segment sliding back and forth
        // Wall-clock phase (~1.75 s period): right however often it's drawn
        float seg = w * 0.3f;
        float t = (sinf((float)(gui_now_ms() % 1745) * (2.0f * (float)M_PI / 1745.0f)) + 1) / 2;
        gui_rrect(x + (w - seg) * t, y, seg, h, h / 2, color);
        return;
    }
    if (frac > 1) frac = 1;
    float fw = w * frac;
    if (fw <= 0) return;
    if (fw < h) fw = h;
    gui_rrect(x, y, fw, h, h / 2, color);
    // Soft highlight on the top half
    if (fw > h) gui_rect(x + h / 2, y + 1, fw - h, h * 0.35f, gui_rgba(0xFFFFFF, 0x30));
}

void gui_spinner(float cx, float cy, float r, u32 color) {
    const int dots = 8;
    int head = (int)((gui_now_ms() / 66) % dots);   // ~0.5 s per turn, wall clock
    for (int i = 0; i < dots; i++) {
        float a = (float)i / dots * 2 * (float)M_PI;
        int age = (head - i + dots) % dots;
        u8 alpha = (u8)(255 - age * 28);
        u32 c = (color & 0x00FFFFFF) | ((u32)alpha << 24);
        gui_circle(cx + cosf(a) * r, cy + sinf(a) * r, r * 0.22f, c);
    }
}

void gui_scrollbar(float x, float y, float h, int first, int visible, int total, float smooth_first) {
    (void)first;
    gui_scrollbar_ex(x, y, h, visible, total, smooth_first, HEX_DIM);
}

void gui_scrollbar_ex(float x, float y, float h, int visible, int total, float smooth_first, u32 thumb_hex) {
    if (total <= visible || total <= 0) return;
    gui_rrect(x, y, 3, h, 1.5f, gui_rgb(HEX_BG2));
    float th = h * visible / total;
    if (th < 12) th = 12;
    float ty = y + (h - th) * (smooth_first / (float)(total - visible));
    if (ty < y) ty = y;
    if (ty > y + h - th) ty = y + h - th;
    gui_rrect(x, ty, 3, th, 1.5f, gui_rgb(thumb_hex));
}

void gui_panel(float x, float y, float w, float h) {
    gui_rrect(x - 1, y - 1, w + 2, h + 2, 8, gui_rgb(0x0A1118));
    gui_rrect(x, y, w, h, 7, gui_rgb(HEX_PANEL));
}

void gui_card(float x, float y, float w, float h, const char *title, u32 tone) {
    // Drop shadow
    gui_rrect(x + 2, y + 3, w, h, 8, gui_rgb(0x070B10));
    gui_rrect(x - 1, y - 1, w + 2, h + 2, 8, gui_mix(HEX_LINE, tone, 0.4f));
    gui_rrect(x, y, w, h, 7, gui_rgb(HEX_PANEL));
    if (title) {
        // Title strip: rounded on top, square at the bottom
        gui_rrect(x, y, w, 24, 7, gui_rgb(HEX_PANEL_HI));
        gui_rect(x, y + 14, w, 10, gui_rgb(HEX_PANEL_HI));
        gui_rect(x, y + 24, w, 1, gui_rgb(HEX_LINE));
        gui_rrect(x + 9, y + 8, 4, 8, 2, gui_rgb(tone));
        gui_text_mid(x + 19, y, 24, GUI_S_BODY, gui_rgb(HEX_TEXT), GUI_LEFT, w - 28, title);
    }
}

void gui_dim(void) {
    gui_rect(0, 0, screen_w, GUI_H, gui_rgba(0x05090D, 0xB4));
}

void gui_header_bar_ex(bool line) {
    gui_rect(0, 0, screen_w, GUI_HEADER_H, gui_rgb(HEX_BG2));
    if (line) gui_rect(0, GUI_HEADER_H - 1, screen_w, 1, gui_rgb(HEX_LINE));
}

void gui_header_bar(void) {
    gui_header_bar_ex(true);
}

void gui_scroll_edges(float x, float w, float top_y, float bottom_y, bool above, bool below) {
    if (above) gui_vgrad(x, top_y, w, 6, gui_rgba(HEX_BG2, 0xFF), gui_rgba(HEX_BG2, 0x00));
    if (below) gui_vgrad(x, bottom_y - 6, w, 6, gui_rgba(HEX_BG2, 0x00), gui_rgba(HEX_BG2, 0xFF));
}

float gui_status_icons(float x_right, float cy) {
    // Server: green answered, red unreachable, grey not asked yet
    int server = network_server_state();
    u32 dot = server > 0 ? HEX_OK : (server == 0 ? HEX_ERR : HEX_MUTED);
    gui_circle(x_right - 4, cy, 5, gui_rgba(dot, 0x50));
    gui_circle(x_right - 4, cy, 3.2f, gui_rgb(dot));
    // WiFi bars
    u8 strength = osGetWifiStrength();
    float bx = x_right - 26;
    for (int i = 0; i < 3; i++) {
        float bh = 4 + i * 3;
        gui_rect(bx + i * 5, cy + 5 - bh, 3, bh, gui_rgb(i < strength ? HEX_TEXT : HEX_MUTED));
    }
    return bx;
}

void gui_header(const char *section) {
    gui_header_bar();
    // Logo mark: teal rounded square with a sync-ish notch
    gui_rrect(8, 6, 14, 14, 4, gui_rgb(HEX_ACCENT));
    gui_rrect(12, 10, 6, 6, 2, gui_rgb(HEX_BG2));
    float x = 28;
    x += gui_text_mid(x, 0, GUI_HEADER_H, 0.55f, gui_rgb(HEX_TEXT), GUI_LEFT, 0, "GameSync");
    if (section && *section) {
        gui_circle(x + 7, GUI_HEADER_H / 2.0f, 1.6f, gui_rgb(HEX_MUTED));
        gui_text_mid(x + 14, 0, GUI_HEADER_H, 0.5f, gui_rgb(HEX_ACCENT2), GUI_LEFT,
                     screen_w - x - 110, section);
    }
    if (screen_id == GUI_TOP) {
        float left = gui_status_icons(screen_w - 8, GUI_HEADER_H / 2.0f);
        gui_text_mid(left - 8, 0, GUI_HEADER_H, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_RIGHT, 0, "v" APP_VERSION);
    }
}

void gui_header_tabs(const char *const *labels, int count, int active) {
    gui_header_bar();
    gui_rrect(8, 6, 14, 14, 4, gui_rgb(HEX_ACCENT));
    gui_rrect(12, 10, 6, 6, 2, gui_rgb(HEX_BG2));
    float cy = GUI_HEADER_H / 2.0f;
    float x = 30;
    x += gui_button(x, cy, "L") + 4;
    static GuiTabsAnim anim;   // one strip, shared by every tab's header
    x = gui_tabs(&anim, x, 4, 18, labels, count, active, NULL) + 4;
    gui_button(x, cy, "R");
    if (screen_id == GUI_TOP) {
        float left = gui_status_icons(screen_w - 8, cy);
        gui_text_mid(left - 8, 0, GUI_HEADER_H, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_RIGHT, 0, "v" APP_VERSION);
    }
}

void gui_footer(const GuiHint *hints, int count) {
    float y = GUI_FOOTER_Y;
    gui_rect(0, y, screen_w, GUI_FOOTER_H, gui_rgb(HEX_BG2));
    gui_rect(0, y, screen_w, 1, gui_rgb(HEX_LINE));
    if (count <= 0) return;
    // Measure, then spread the leftover space between items
    float widths[12], total = 0;
    if (count > 12) count = 12;
    for (int i = 0; i < count; i++) {
        widths[i] = gui_button_w(hints[i].button) + 4 + gui_text_w(GUI_S_SMALL, hints[i].label);
        total += widths[i];
    }
    float gap = (screen_w - 16 - total) / (count > 1 ? count - 1 : 1);
    if (gap > 16) gap = 16;
    if (gap < 4) gap = 4;
    float x = 8, cy = y + GUI_FOOTER_H / 2.0f + 0.5f;
    for (int i = 0; i < count; i++) {
        float bw = gui_button(x, cy, hints[i].button);
        gui_text_mid(x + bw + 4, y, GUI_FOOTER_H, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, 0, hints[i].label);
        x += widths[i] + gap;
    }
}

#define TAB_PAD 9

float gui_tabs_layout(float x, float y, float h, const char *const *labels, int count, GuiRect *rects) {
    float cx = x + 2;
    if (count > GUI_TABS_MAX) count = GUI_TABS_MAX;
    for (int i = 0; i < count; i++) {
        float w = gui_text_w(GUI_S_SMALL, labels[i]) + TAB_PAD * 2;
        if (rects) rects[i] = gui_rect_make(cx, y, w, h);
        cx += w;
    }
    return cx + 2;
}

float gui_tabs(GuiTabsAnim *a, float x, float y, float h, const char *const *labels, int count, int active,
               GuiRect *rects) {
    GuiRect seg[GUI_TABS_MAX];
    if (count > GUI_TABS_MAX) count = GUI_TABS_MAX;
    float right = gui_tabs_layout(x, y, h, labels, count, seg);
    if (rects)
        for (int i = 0; i < count; i++) rects[i] = seg[i];
    gui_rrect(x, y, right - x, h, h / 2, gui_rgb(HEX_BG));
    if (count <= 0) return right;

    bool on = active >= 0 && active < count;
    float tx = on ? seg[active].x - x : 0, tw = on ? seg[active].w : 0;
    float px = tx, pw = tw;
    if (a) {
        if (!a->init || a->count != count) {
            gui_spring_set(&a->x, tx);
            gui_spring_set(&a->w, tw);
            a->count = count;
            a->init = true;
        }
        // Once a frame; a retarget mid-flight continues from where the pill is
        if (a->frame != ticks) {
            a->frame = ticks;
            gui_spring(&a->x, tx, GUI_RESP_PILL, frame_dt);
            gui_spring(&a->w, tw, GUI_RESP_PILL, frame_dt);
        }
        px = a->x.x;
        pw = a->w.x;
        // A finger on another segment: a soft press under its label
        int held = a->held - 1;
        if (held >= 0 && held < count && held != active)
            gui_rrect(seg[held].x, y + 2, seg[held].w, h - 4, (h - 4) / 2, gui_mix(HEX_BG, HEX_ACCENT, 0.25f));
    }
    if (on && pw > 0) gui_rrect(x + px, y + 2, pw, h - 4, (h - 4) / 2, gui_rgb(HEX_ACCENT));
    for (int i = 0; i < count; i++) {
        // Label colour by how much of its segment the pill covers
        float s0 = seg[i].x - x, s1 = s0 + seg[i].w;
        float o0 = s0 > px ? s0 : px, o1 = s1 < px + pw ? s1 : px + pw;
        float cover = on && o1 > o0 ? (o1 - o0) / seg[i].w : 0;
        gui_text_mid(seg[i].x + seg[i].w / 2, y, h, GUI_S_SMALL, gui_mix(HEX_DIM, HEX_INK, cover), GUI_CENTER, 0,
                     labels[i]);
    }
    return right;
}
