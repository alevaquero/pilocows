#!/usr/bin/env python3
"""Regenerates main/img_splash_logo.{c,h} from the frontend app icon.

Converts frontend/src-tauri/icons/icon.png to a raw RGB565 lv_img_dsc_t C
array for the boot splash screen (screen_splash.c). Re-run this whenever
the app icon changes. Requires Pillow (`pip install pillow`); the ESP-IDF
python env doesn't have it, so this is meant to be run with a plain
system python3, not `idf.py`'s.
"""
from pathlib import Path
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC = REPO_ROOT.parent / "frontend" / "src-tauri" / "icons" / "icon.png"
OUT_C = REPO_ROOT / "main" / "img_splash_logo.c"
OUT_H = REPO_ROOT / "main" / "img_splash_logo.h"

SIZE = 280
BG = (30, 41, 59)  # matches the icon's own navy background, sampled from its corner pixel


def main():
    im = Image.open(SRC).convert("RGBA")
    # Flatten onto the same navy the icon already uses, in case of any
    # antialiased edge alpha, then resize with high-quality resampling.
    flat = Image.new("RGB", im.size, BG)
    flat.paste(im, mask=im.split()[3])
    flat = flat.resize((SIZE, SIZE), Image.LANCZOS)

    pixels = flat.load()
    raw = bytearray()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b = pixels[x, y]
            val = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)  # RGB565
            raw.append(val & 0xFF)         # low byte first (LV_COLOR_16_SWAP == 0)
            raw.append((val >> 8) & 0xFF)  # high byte

    lines = []
    for i in range(0, len(raw), 16):
        chunk = raw[i:i + 16]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")

    c_source = f"""// Generated from frontend/src-tauri/icons/icon.png ({SIZE}x{SIZE}, RGB565,
// no alpha — the source icon is fully opaque) for the boot splash screen.
// Regenerate with tools/gen_splash_image.py if the app icon changes; this
// file is not meant to be hand-edited.
#include "img_splash_logo.h"

static const uint8_t splash_logo_map[] = {{
{chr(10).join(lines)}
}};

const lv_img_dsc_t img_splash_logo = {{
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = {SIZE},
    .header.h = {SIZE},
    .data_size = {len(raw)},
    .data = splash_logo_map,
}};
"""

    h_source = """#ifndef IMG_SPLASH_LOGO_H
#define IMG_SPLASH_LOGO_H

#include "lvgl.h"

// Pilo's Cows app icon, pre-converted to RGB565 for the boot splash screen
// (screen_splash.c). Regenerate with tools/gen_splash_image.py.
extern const lv_img_dsc_t img_splash_logo;

#endif
"""

    OUT_C.write_text(c_source)
    OUT_H.write_text(h_source)
    print(f"wrote {OUT_C} ({len(raw)} bytes of pixel data)")
    print(f"wrote {OUT_H}")


if __name__ == "__main__":
    main()
