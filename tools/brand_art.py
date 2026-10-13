#!/usr/bin/env python3
"""Draws GameSync's logo and every client's icon, banner and background.

    python tools/brand_art.py [--texconv path/to/texconv.exe] [--preview DIR]

One design for every client: two sync arrows chasing each other round a game
pad, in a cyan -> blue -> violet gradient on night blue, with the "GameSync"
wordmark (Game in white, Sync in the gradient). Everything is drawn here from
code, so it can be changed in one place and re-run. Needs Pillow and numpy.

Writes, in each client's own format:

  assets/brand/           gamesync-icon.png (1024), gamesync-mark.png (1024,
                          transparent), gamesync-banner.png (README header)
  3ds/                    icon.png 48x48 (SMDH), banner_img.png 256x128 (CIA banner)
  ds/icon.bmp             32x32, 16 colours, 4 bits per pixel (ndstool)
  psp/                    ICON0.PNG 144x80, PIC1.PNG 480x272 (EBOOT.PBP)
  vita/sce_sys/           icon0.png 128x128, livearea/contents/bg.png 840x500
                          and startup.png 280x158, all 8-bit indexed as the
                          LiveArea requires
  ps3/assets/             ICON0.PNG 320x176, PIC1.PNG 1920x1080 (PKG)
  ps5/sce_sys/            icon0.png 512x512; with --texconv (Microsoft
                          DirectXTex, https://github.com/microsoft/DirectXTex)
                          pic0.dds / pic1.dds, 3840x2160 BC7 DX10, no mipmaps
  wiiu/meta/              icon.png 128x128, tv-splash.png 1280x720,
                          drc-splash.png 854x480 (.wuhb, via wuhbtool)
  android/.../res/drawable-nodpi/  ic_launcher_foreground.png and
                          ic_launcher_background.png (adaptive icon, 432x432)
  desktop/assets/, steamdeck/assets/  gamesync.png 256x256 (window icon)
  server/app/templates/favicon.png    64x64

--preview DIR also keeps the 4K PS5 PNGs and small previews of each there.
Font: Vegur (public domain, xbox/assets/font.ttf).
"""
import argparse
import math
import os
import struct
import subprocess
import tempfile

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "xbox", "assets", "font.ttf")
TAGLINE = "Save sync for every console"

# palette: night blue ground, a cyan -> blue -> violet accent
BG_TOP = (9, 13, 26)
BG_BOTTOM = (20, 16, 44)
CYAN = (0, 214, 255)
BLUE = (58, 120, 255)
VIOLET = (164, 92, 255)
WHITE = (238, 242, 250)
PAD_SHADE = (196, 206, 228)
INK = (22, 28, 52)


def lerp(a, b, t):
    return tuple(int(round(x + (y - x) * t)) for x, y in zip(a, b))


def accent(t):
    """The accent gradient, t in 0..1."""
    return lerp(CYAN, BLUE, t * 2) if t < 0.5 else lerp(BLUE, VIOLET, (t - 0.5) * 2)


def out_path(*parts):
    path = os.path.join(ROOT, *parts)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    return path


# ------------------------------------------------------------------ the mark

def draw_mark(size, glow=True, simple=False):
    """The logo mark on a transparent square, drawn 3x and scaled down.

    simple: a bolder ring and no face buttons, for icons of 48 px and less."""
    ss = 3 if size >= 128 else 8
    S = size * ss
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = S / 2
    R = S * (0.36 if simple else 0.37)  # ring radius (centre of the stroke)
    T = S * (0.12 if simple else 0.085)  # ring thickness

    # two arcs, each ending in an arrowhead; colours run round the ring
    for start, end, t0 in ((-150, -18, 0.0), (30, 162, 0.5)):
        steps = int((end - start) * 2)
        box = (c - R - T / 2, c - R - T / 2, c + R + T / 2, c + R + T / 2)
        for i in range(steps):
            a0 = start + (end - start) * i / steps
            a1 = start + (end - start) * (i + 1.6) / steps
            d.arc(box, a0, a1, fill=accent(t0 + 0.5 * i / steps) + (255,), width=int(T))
        # arrowhead pointing on round the ring (clockwise on screen)
        a = math.radians(end)
        px, py = c + R * math.cos(a), c + R * math.sin(a)
        rx, ry = math.cos(a), math.sin(a)
        tx, ty = -math.sin(a), math.cos(a)
        w = T * (1.0 if simple else 1.15)
        tip = (px + tx * T * 1.6, py + ty * T * 1.6)
        b1 = (px + rx * w - tx * T * 0.2, py + ry * w - ty * T * 0.2)
        b2 = (px - rx * w - tx * T * 0.2, py - ry * w - ty * T * 0.2)
        d.polygon([b1, tip, b2], fill=accent(t0 + 0.5) + (255,))

    # the game pad
    pw, ph = S * (0.46 if simple else 0.44), S * (0.23 if simple else 0.215)
    x0, y0 = c - pw / 2, c - ph / 2 - S * 0.012
    grip_r = ph * 0.52
    pad = Image.new("L", (S, S), 0)
    pd = ImageDraw.Draw(pad)
    pd.rounded_rectangle((x0, y0, x0 + pw, y0 + ph), radius=ph * 0.48, fill=255)
    for gx in (x0 + grip_r * 0.95, x0 + pw - grip_r * 0.95):
        pd.ellipse((gx - grip_r, y0 + ph * 0.25, gx + grip_r, y0 + ph * 0.25 + grip_r * 2.05),
                   fill=255)
    shade = Image.linear_gradient("L").resize((S, S))
    body = Image.composite(Image.new("RGBA", (S, S), WHITE + (255,)),
                           Image.new("RGBA", (S, S), PAD_SHADE + (255,)),
                           ImageChops.invert(shade))
    img.paste(body, (0, 0), pad)
    d = ImageDraw.Draw(img)
    # d-pad
    dx, dy = x0 + pw * 0.27, y0 + ph * 0.48
    arm, thick = ph * (0.32 if simple else 0.30), ph * (0.15 if simple else 0.13)
    d.rounded_rectangle((dx - arm, dy - thick, dx + arm, dy + thick), radius=thick * 0.4,
                        fill=INK + (255,))
    d.rounded_rectangle((dx - thick, dy - arm, dx + thick, dy + arm), radius=thick * 0.4,
                        fill=INK + (255,))
    # face buttons in the accent colours (one big dot when small)
    bx, by = x0 + pw * 0.73, y0 + ph * 0.48
    if simple:
        br = ph * 0.2
        d.ellipse((bx - br, by - br, bx + br, by + br), fill=VIOLET + (255,))
    else:
        br, off = ph * 0.105, ph * 0.21
        for (ox, oy), t in (((0, -off), 0.0), ((off, 0), 0.33), ((0, off), 0.66), ((-off, 0), 1.0)):
            d.ellipse((bx + ox - br, by + oy - br, bx + ox + br, by + oy + br),
                      fill=accent(t) + (255,))

    mark = img.resize((size, size), Image.LANCZOS)
    if not glow:
        return mark
    halo = mark.split()[3].filter(ImageFilter.GaussianBlur(max(1, size * 0.035)))
    glow_img = Image.new("RGBA", (size, size), BLUE + (0,))
    glow_img.putalpha(halo.point(lambda v: int(v * 0.55)))
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.alpha_composite(glow_img)
    out.alpha_composite(mark)
    return out


# ------------------------------------------------------------------ wordmark

def gradient_fill(w, h):
    t = np.linspace(0, 1, max(w, 1))
    cols = np.array([accent(v) for v in t], dtype=np.uint8)
    return Image.fromarray(np.repeat(cols[None, :, :], max(h, 1), axis=0), "RGB")


def draw_wordmark(height, tagline=None):
    """"GameSync": Game in white, Sync in the accent gradient; RGBA, tight."""
    ss = 1 if height >= 60 else 4
    font = ImageFont.truetype(FONT, int(height * ss))
    bold = max(1, int(height * ss * 0.035))
    game_w = int(font.getlength("Game"))
    sync_w = int(font.getlength("Sync"))
    gap = int(height * ss * 0.02)
    w = game_w + gap + sync_w + bold * 4
    asc, desc = font.getmetrics()
    h = asc + desc + bold * 4
    tag_font, tag_h = None, 0
    if tagline:
        tag_font = ImageFont.truetype(FONT, int(height * ss * 0.24))
        tag_h = int(height * ss * 0.42)
        tag_text = tagline.upper()
        spacing = height * ss * 0.045
        tag_w = sum(tag_font.getlength(ch) for ch in tag_text) + spacing * (len(tag_text) - 1)
        w = max(w, int(tag_w) + bold * 8)
    img = Image.new("RGBA", (w, h + tag_h), (0, 0, 0, 0))
    x0 = (w - (game_w + gap + sync_w)) // 2
    d = ImageDraw.Draw(img)
    d.text((x0, bold * 2), "Game", font=font, fill=WHITE + (255,), stroke_width=bold,
           stroke_fill=WHITE + (255,))
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).text((x0 + game_w + gap, bold * 2), "Sync", font=font, fill=255,
                              stroke_width=bold, stroke_fill=255)
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    layer.paste(gradient_fill(sync_w + bold * 2, img.size[1]), (x0 + game_w + gap - bold, 0))
    layer.putalpha(mask)
    img.alpha_composite(layer)
    if tagline:
        x, y = (w - tag_w) / 2, h + tag_h * 0.05
        for ch in tag_text:
            d.text((x, y), ch, font=tag_font, fill=(150, 168, 205, 255))
            x += tag_font.getlength(ch) + spacing
    img = img.crop(img.getbbox())
    if ss > 1:
        img = img.resize((max(1, img.width // ss), max(1, img.height // ss)), Image.LANCZOS)
    return img


# ------------------------------------------------------------------ grounds

def ground(w, h, glows):
    """Vertical gradient plus soft coloured glows: [(cx, cy, radius, colour, strength)]."""
    y = np.linspace(0, 1, h)[:, None, None]
    top = np.array(BG_TOP, dtype=np.float32)
    bottom = np.array(BG_BOTTOM, dtype=np.float32)
    arr = np.broadcast_to(top + (bottom - top) * y, (h, w, 3)).astype(np.float32).copy()
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    for cx, cy, r, col, k in glows:
        dist = ((xx - cx * w) ** 2 + (yy - cy * h) ** 2) / (r * max(w, h)) ** 2
        arr += np.exp(-dist)[..., None] * np.array(col, dtype=np.float32) * k
    return arr


def add_floor(arr, horizon, colour, strength):
    """A retro perspective grid fading in from the horizon to the bottom edge."""
    h, w, _ = arr.shape
    layer = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(layer)
    hy = int(h * horizon)
    vx = w * 0.5
    lw = max(1, round(h / 720))
    for i in range(-24, 25):
        d.line((vx + i * w * 0.004, hy, vx + i * w * 0.09, h), fill=255, width=lw)
    z = 1.0
    while hy + (h - hy) / z - hy >= 3:
        y = hy + (h - hy) / z
        d.line((0, y, w, y), fill=255, width=lw)
        z *= 1.28
    fade = np.clip((np.arange(h) - hy) / max(1, h - hy), 0, 1) ** 1.6
    a = np.asarray(layer, dtype=np.float32) / 255 * fade[:, None]
    arr += a[..., None] * np.array(colour, dtype=np.float32) * strength


def add_pixels(arr, seed, count, region):
    """Small floating squares, like stray pixels, in region (x0, y0, x1, y1) fractions."""
    h, w, _ = arr.shape
    rng = np.random.default_rng(seed)
    x0, y0, x1, y1 = region
    unit = h / 2160
    for _ in range(count):
        s = max(1, int(rng.uniform(6, 22) * unit))
        x = int(rng.uniform(x0, x1) * w)
        y = int(rng.uniform(y0, y1) * h)
        col = np.array(accent(rng.uniform(0, 1)), dtype=np.float32)
        k = rng.uniform(0.08, 0.35)
        arr[y:y + s, x:x + s] = arr[y:y + s, x:x + s] * (1 - k) + col * k


def to_image(arr):
    # a hint of noise keeps the gradients from banding
    noise = np.random.default_rng(7).normal(0, 1.2, arr.shape[:2])[..., None]
    return Image.fromarray(np.clip(arr + noise, 0, 255).astype(np.uint8), "RGB").convert("RGBA")


def place(img, layer, cx, cy):
    img.alpha_composite(layer, (int(cx - layer.width / 2), int(cy - layer.height / 2)))


# ------------------------------------------------------------------ compositions

def scene_selected(w, h, floor=True):
    """The mark and name on the right: PS5 pic0, where the Shell draws the
    title and Play button on the left; also the PS3's XMB background."""
    arr = ground(w, h, [(0.74, 0.42, 0.42, BLUE, 0.30), (0.95, 0.10, 0.30, VIOLET, 0.22),
                        (0.10, 0.95, 0.35, CYAN, 0.08)])
    if floor:
        add_floor(arr, 0.70, CYAN, 0.16)
    add_pixels(arr, 1, 70, (0.45, 0.05, 0.98, 0.65))
    img = to_image(arr)
    place(img, draw_mark(int(h * 0.50)), w * 0.735, h * 0.40)
    word = draw_wordmark(h * 0.085, TAGLINE)
    img.alpha_composite(word, (int(w * 0.735 - word.width / 2), int(h * 0.68)))
    return img.convert("RGB")


def scene_centred(w, h, floor=True, tagline=True, mark_frac=0.44, word_frac=0.10, pixels=90):
    """Mark and name in the middle: launch screens and splashes."""
    arr = ground(w, h, [(0.50, 0.42, 0.38, BLUE, 0.30), (0.85, 0.15, 0.25, VIOLET, 0.18),
                        (0.15, 0.20, 0.25, CYAN, 0.10)])
    if floor:
        add_floor(arr, 0.72, VIOLET, 0.14)
    add_pixels(arr, 2, pixels, (0.03, 0.05, 0.97, 0.66))
    img = to_image(arr)
    place(img, draw_mark(int(h * mark_frac)), w / 2, h * 0.36)
    word = draw_wordmark(h * word_frac, TAGLINE if tagline else None)
    img.alpha_composite(word, (int(w / 2 - word.width / 2), int(h * 0.62)))
    return img.convert("RGB")


def scene_side(w, h, transparent=False, tagline=False):
    """Mark on the left, name on the right: wide icons and banners."""
    if transparent:
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    else:
        img = to_image(ground(w, h, [(0.25, 0.5, 0.35, BLUE, 0.32), (0.9, 0.1, 0.3, VIOLET, 0.2)]))
    m = draw_mark(int(h * 0.92), glow=h >= 120, simple=h < 100)
    place(img, m, w * 0.04 + m.width / 2, h / 2)
    word = draw_wordmark(h * 0.30, TAGLINE if tagline else None)
    room = w - (w * 0.04 + m.width) - w * 0.04
    if word.width > room:
        word = word.resize((int(room), int(word.height * room / word.width)), Image.LANCZOS)
    place(img, word, w * 0.04 + m.width + room / 2, h / 2)
    return img if transparent else img.convert("RGB")


def tile(size, wordmark=True, simple=None):
    """A square app icon: the mark on the ground, the name under it if it fits."""
    if simple is None:
        simple = size <= 64
    img = to_image(ground(size, size, [(0.5, 0.42, 0.55, BLUE, 0.32),
                                       (0.9, 0.05, 0.4, VIOLET, 0.20)]))
    if wordmark and size >= 128:
        mark = draw_mark(int(size * 0.80))
        img.alpha_composite(mark, ((size - mark.width) // 2, int(size * 0.04)))
        word = draw_wordmark(size * 0.13)
        place(img, word, size / 2, size * 0.87)
    else:
        mark = draw_mark(int(size * (1.0 if simple else 0.92)), glow=size >= 96, simple=simple)
        place(img, mark, size / 2, size / 2)
    return img.convert("RGB")


def banner(w=1600, h=440):
    """The README header."""
    arr = ground(w, h, [(0.30, 0.5, 0.30, BLUE, 0.30), (0.85, 0.2, 0.25, VIOLET, 0.22)])
    add_floor(arr, 0.66, CYAN, 0.12)
    add_pixels(arr, 3, 50, (0.02, 0.05, 0.98, 0.6))
    img = to_image(arr)
    m = draw_mark(int(h * 0.78))
    place(img, m, w * 0.27, h / 2)
    word = draw_wordmark(h * 0.24, TAGLINE)
    place(img, word, w * 0.62, h / 2)
    return img.convert("RGB")


# ------------------------------------------------------------------ writers

def save_indexed(img, path, colours=256):
    """8-bit palette PNG (the Vita's LiveArea takes nothing else)."""
    img.convert("RGB").quantize(colours, method=Image.Quantize.MEDIANCUT,
                                dither=Image.Dither.FLOYDSTEINBERG).save(path, optimize=True)


def save_bmp4(img, path):
    """32x32, 16 colours, 4 bits per pixel BMP for ndstool. Palette entry 0 is
    the DS icon's transparent colour, so it holds an unused magenta."""
    q = img.convert("RGB").quantize(15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:45]
    pal = [255, 0, 255] + pal + [0] * (48 - 3 - len(pal))
    idx = np.asarray(q, dtype=np.uint8) + 1
    w, h = img.size
    rows = []
    for y in range(h - 1, -1, -1):  # bottom-up
        row = idx[y]
        rows.append(bytes((int(row[x]) << 4) | int(row[x + 1]) for x in range(0, w, 2)))
    pixels = b"".join(rows)
    palette = b"".join(bytes((pal[i * 3 + 2], pal[i * 3 + 1], pal[i * 3], 0)) for i in range(16))
    offset = 14 + 40 + len(palette)
    header = b"BM" + struct.pack("<IHHI", offset + len(pixels), 0, 0, offset)
    dib = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 4, 0, len(pixels), 2835, 2835, 16, 16)
    with open(path, "wb") as f:
        f.write(header + dib + palette + pixels)


def to_dds(texconv, png, out_dir, name):
    subprocess.run([texconv, "-nologo", "-y", "-f", "BC7_UNORM", "-m", "1", "-dx10",
                    "-w", "3840", "-h", "2160", "-o", out_dir, png], check=True,
                   stdout=subprocess.DEVNULL)
    made = os.path.join(out_dir, os.path.splitext(os.path.basename(png))[0] + ".dds")
    os.replace(made, os.path.join(out_dir, name))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--texconv", help="texconv.exe, to write the PS5's pic0.dds and pic1.dds")
    ap.add_argument("--preview", help="folder for the 4K PNGs and small previews")
    args = ap.parse_args()
    preview = args.preview
    if preview:
        os.makedirs(preview, exist_ok=True)
    written = []

    def save(img, *parts, indexed=False):
        path = out_path(*parts)
        if indexed:
            save_indexed(img, path)
        else:
            img.save(path, optimize=True)
        written.append(path)
        if preview:
            img.save(os.path.join(preview, "_".join(parts).replace(os.sep, "_")))

    # shared
    big = tile(1024)
    save(big, "assets", "brand", "gamesync-icon.png")
    save(draw_mark(1024), "assets", "brand", "gamesync-mark.png")
    save(banner(), "assets", "brand", "gamesync-banner.png")
    # 3DS: SMDH icon, CIA banner (transparent, the banner frame shows round it)
    save(tile(48), "3ds", "icon.png")
    save(scene_side(256, 128, transparent=True), "3ds", "banner_img.png")
    # DS
    ds = out_path("ds", "icon.bmp")
    save_bmp4(tile(32), ds)
    written.append(ds)
    # PSP
    save(scene_side(144, 80), "psp", "ICON0.PNG")
    save(scene_centred(480, 272, floor=True, tagline=False, pixels=40), "psp", "PIC1.PNG")
    # Vita
    save(tile(128, wordmark=False), "vita", "sce_sys", "icon0.png", indexed=True)
    save(scene_centred(840, 500, tagline=True, pixels=50), "vita", "sce_sys", "livearea",
         "contents", "bg.png", indexed=True)
    save(scene_side(280, 158), "vita", "sce_sys", "livearea", "contents", "startup.png",
         indexed=True)
    # PS3
    save(scene_side(320, 176), "ps3", "assets", "ICON0.PNG")
    save(scene_selected(1920, 1080), "ps3", "assets", "PIC1.PNG")
    # PS5
    save(tile(512), "ps5", "sce_sys", "icon0.png")
    # Wii U (.wuhb)
    save(tile(128, wordmark=False), "wiiu", "meta", "icon.png")
    save(scene_centred(1280, 720), "wiiu", "meta", "tv-splash.png")
    save(scene_centred(854, 480), "wiiu", "meta", "drc-splash.png")
    # Android adaptive icon: the mark inside the 66/108 safe zone, the ground behind
    res = ("android", "app", "src", "main", "res", "drawable-nodpi")
    fg = Image.new("RGBA", (432, 432), (0, 0, 0, 0))
    place(fg, draw_mark(int(432 * 0.62)), 216, 216)
    save(fg, *res, "ic_launcher_foreground.png")
    save(to_image(ground(432, 432, [(0.5, 0.42, 0.55, BLUE, 0.32),
                                    (0.9, 0.05, 0.4, VIOLET, 0.20)])).convert("RGB"),
         *res, "ic_launcher_background.png")
    # desktop, Steam Deck window icons; the server's favicon
    small = tile(256, wordmark=False)
    save(small, "desktop", "assets", "gamesync.png")
    save(small, "steamdeck", "assets", "gamesync.png")
    save(tile(64, wordmark=False), "server", "app", "templates", "favicon.png")

    # PS5 backgrounds
    work = preview or tempfile.mkdtemp()
    sel, launch = os.path.join(work, "ps5_pic0.png"), os.path.join(work, "ps5_pic1.png")
    scene_selected(3840, 2160).save(sel)
    scene_centred(3840, 2160).save(launch)
    if preview:
        for p in (sel, launch):
            Image.open(p).resize((1280, 720), Image.LANCZOS).save(p[:-4] + "_small.png")
    if args.texconv:
        sce = os.path.join(ROOT, "ps5", "sce_sys")
        to_dds(args.texconv, sel, sce, "pic0.dds")
        to_dds(args.texconv, launch, sce, "pic1.dds")
        written += [os.path.join(sce, "pic0.dds"), os.path.join(sce, "pic1.dds")]
    for p in written:
        print(os.path.relpath(p, ROOT))


if __name__ == "__main__":
    main()
