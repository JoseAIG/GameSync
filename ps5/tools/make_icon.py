#!/usr/bin/env python3
"""Draws ps5/sce_sys/icon0.png (512x512) with the UI font. Needs Pillow.

    python ps5/tools/make_icon.py
"""
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
font = os.path.join(ROOT, "xbox", "assets", "font.ttf")
img = Image.new("RGB", (512, 512), (14, 18, 23))
d = ImageDraw.Draw(img)
d.rounded_rectangle((56, 56, 456, 456), radius=72, fill=(43, 76, 126))
d.text((256, 236), "GS", font=ImageFont.truetype(font, 210), fill=(232, 236, 240), anchor="mm")
d.text((256, 392), "GameSync", font=ImageFont.truetype(font, 58), fill=(90, 169, 255), anchor="mm")
img.save(os.path.join(ROOT, "ps5", "sce_sys", "icon0.png"))
