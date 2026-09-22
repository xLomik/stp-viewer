#!/usr/bin/env python3
"""Genera assets/stpviewer.ico: un cubo isometrico con un agujero, en los
tamanos que pide Windows. Requiere Pillow.

    python3 tests/make_icon.py assets/stpviewer.ico
"""

import math
import os
import sys

from PIL import Image, ImageDraw

STEEL_TOP = (196, 208, 218, 255)
STEEL_LEFT = (150, 164, 178, 255)
STEEL_RIGHT = (120, 134, 148, 255)
LINE = (44, 56, 68, 255)


def draw_cube(size):
    scale = 8
    w = size * scale
    img = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    cx, cy = w / 2, w / 2
    r = w * 0.40          # medio ancho
    h = w * 0.22          # altura del lado
    k = 0.5               # aplanado isometrico

    top = [(cx, cy - h - r * k), (cx + r, cy - h), (cx, cy - h + r * k), (cx - r, cy - h)]
    left = [(cx - r, cy - h), (cx, cy - h + r * k), (cx, cy + h + r * k), (cx - r, cy + h)]
    right = [(cx + r, cy - h), (cx, cy - h + r * k), (cx, cy + h + r * k), (cx + r, cy + h)]

    width = max(1, int(w * 0.012))
    d.polygon(left, fill=STEEL_LEFT, outline=LINE, width=width)
    d.polygon(right, fill=STEEL_RIGHT, outline=LINE, width=width)
    d.polygon(top, fill=STEEL_TOP, outline=LINE, width=width)

    # agujero en la cara superior
    hole_r = r * 0.34
    box = [cx - hole_r, cy - h - hole_r * k, cx + hole_r, cy - h + hole_r * k]
    d.ellipse(box, fill=(96, 110, 124, 255), outline=LINE, width=width)
    inner = hole_r * 0.92
    d.ellipse([cx - inner, cy - h - inner * k + width, cx + inner, cy - h + inner * k + width],
              fill=(72, 84, 96, 255))

    return img.resize((size, size), Image.LANCZOS)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "assets/stpviewer.ico"
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [draw_cube(s) for s in sizes]
    images[0].save(out, format="ICO", sizes=[(s, s) for s in sizes], append_images=images[1:])
    print("icono:", out)


if __name__ == "__main__":
    main()
