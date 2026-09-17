"""Generates every logo file from one source: assets/deck_thing.svg.

Usage: python tools/make_logo.py

- companion/assets/deck_thing.ico  app, taskbar, tray, installer (16–256 px)
- companion/assets/deck_thing.png  app sidebar, web pages (256 px)
- docs/logo.png                     README (512 px)
- firmware/ui/images/logo_*.c/.h    parts of the start animation on the device (RGB565A8)

The logo is made only of rectangles, circles and ring segments; Pillow redraws it here so
building needs no SVG renderer (Cairo, a browser). If the SVG changes, update the
measurements below.
"""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
REPO = ROOT.parent
GREEN = (0x1E, 0xD7, 0x60, 255)
INK = (0x12, 0x12, 0x12, 255)
SS = 4                     # supersampling: draw large, then shrink = smooth edges
CANVAS = 1024
TILT = 6                   # SVG: rotate(-6) – the group tilts counter-clockwise
DIAL = (170, 0)            # centre of the dial in group coordinates
BARS = [(-285, 10, 40, 140), (-205, -70, 40, 220), (-125, -20, 40, 170)]
DEVICE_LOGO = 300          # edge length on the display in pixels


def layer() -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGBA", (CANVAS * SS, CANVAS * SS), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def g(x: float, y: float) -> tuple[float, float]:
    """Group coordinates (origin at the centre) → canvas."""
    return ((512 + x) * SS, (512 + y) * SS)


def rrect(d: ImageDraw.ImageDraw, x: float, y: float, w: float, h: float, r: float, fill) -> None:
    (x0, y0), (x1, y1) = g(x, y), g(x + w, y + h)
    d.rounded_rectangle((x0, y0, x1, y1), radius=r * SS, fill=fill)


def circle(d: ImageDraw.ImageDraw, cx: float, cy: float, r: float, fill) -> None:
    (x0, y0), (x1, y1) = g(cx - r, cy - r), g(cx + r, cy + r)
    d.ellipse((x0, y0, x1, y1), fill=fill)


def ring_segments(d: ImageDraw.ImageDraw) -> None:
    """Eight segments between r=95 and r=165, ±16° each, starting at the top."""
    for k in range(8):
        mid = -90 + 45 * k
        outer = [(DIAL[0] + 165 * math.cos(math.radians(a)), DIAL[1] + 165 * math.sin(math.radians(a)))
                 for a in [mid - 16 + i * 32 / 24 for i in range(25)]]
        inner = [(DIAL[0] + 95 * math.cos(math.radians(a)), DIAL[1] + 95 * math.sin(math.radians(a)))
                 for a in [mid + 16 - i * 32 / 24 for i in range(25)]]
        d.polygon([g(x, y) for x, y in outer + inner], fill=GREEN)


def tilt(img: Image.Image) -> Image.Image:
    return img.rotate(TILT, resample=Image.Resampling.BICUBIC, center=(512 * SS, 512 * SS))


def shrink(img: Image.Image, size: int) -> Image.Image:
    return img.resize((size, size), Image.Resampling.LANCZOS)


def background() -> Image.Image:
    img, d = layer()
    d.rounded_rectangle((0, 0, CANVAS * SS, CANVAS * SS), radius=200 * SS, fill=INK)
    return img


def body(with_bars: bool, with_dial: bool) -> Image.Image:
    img, d = layer()
    rrect(d, -370, -230, 740, 460, 90, GREEN)
    rrect(d, -330, -190, 290, 380, 40, INK)
    if with_bars:
        for x, y, w, h in BARS:
            rrect(d, x, y, w, h, 14, GREEN)
    circle(d, *DIAL, 180, INK)
    if with_dial:
        ring_segments(d)
        circle(d, *DIAL, 70, GREEN)
    return tilt(img)


def full_logo() -> Image.Image:
    img = background()
    img.alpha_composite(body(True, True))
    return img


# ---------- App and README ----------

def write_app_assets(logo: Image.Image) -> None:
    assets = ROOT / "assets"
    shrink(logo, 256).save(assets / "deck_thing.png")
    shrink(logo, 256).save(assets / "deck_thing.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    (REPO / "docs").mkdir(exist_ok=True)
    shrink(logo, 512).save(REPO / "docs" / "logo.png")


# ---------- Device: parts for the start animation ----------

def rgb565a8(img: Image.Image) -> bytes:
    raw = img.convert("RGBA").tobytes()
    rgb = bytearray()
    for i in range(0, len(raw), 4):
        value = (raw[i] >> 3) << 11 | (raw[i + 1] >> 2) << 5 | (raw[i + 2] >> 3)
        rgb += value.to_bytes(2, "little")
    return bytes(rgb) + raw[3::4]


def c_image(name: str, img: Image.Image) -> str:
    data = rgb565a8(img)
    rows = ",\n".join("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 20]) for i in range(0, len(data), 20))
    w, h = img.size
    return (f"static const uint8_t {name}_data[] = {{\n{rows}\n}};\n\n"
            f"const lv_image_dsc_t {name} = {{\n"
            f"    .header = {{ .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8, .w = {w}, .h = {h}, .stride = {w * 2} }},\n"
            f"    .data_size = sizeof({name}_data),\n    .data = {name}_data,\n}};\n\n")


def to_device(img: Image.Image) -> Image.Image:
    return shrink(img, DEVICE_LOGO)


def crop_part(img: Image.Image) -> tuple[Image.Image, tuple[int, int]]:
    box = img.getchannel("A").getbbox()
    return img.crop(box), (box[0], box[1])


def write_device_images() -> None:
    scale = DEVICE_LOGO / CANVAS
    base = background()
    base.alpha_composite(body(False, False))
    base = to_device(base)

    parts = []
    for i, (x, y, w, h) in enumerate(BARS):
        img, d = layer()
        rrect(d, x, y, w, h, 14, GREEN)
        part, pos = crop_part(to_device(tilt(img)))
        # pivot for the bounce: bottom edge of the bar (tilted), relative to the crop
        bx, by = x + w / 2, y + h
        ang = math.radians(-TILT)
        px = 512 + bx * math.cos(ang) - by * math.sin(ang)
        py = 512 + bx * math.sin(ang) + by * math.cos(ang)
        parts.append((f"logo_bar{i + 1}", part, pos, (round(px * scale) - pos[0], round(py * scale) - pos[1])))

    img, d = layer()
    ring_segments(d)
    circle(d, *DIAL, 70, GREEN)
    dial_full = to_device(tilt(img))
    ang = math.radians(-TILT)
    cx = (512 + DIAL[0] * math.cos(ang) - DIAL[1] * math.sin(ang)) * scale
    cy = (512 + DIAL[0] * math.sin(ang) + DIAL[1] * math.cos(ang)) * scale
    half = math.ceil(166 * scale) + 1
    box = (round(cx) - half, round(cy) - half, round(cx) + half, round(cy) + half)
    dial = dial_full.crop(box)

    out = REPO / "firmware" / "ui" / "images"
    out.mkdir(exist_ok=True)
    src = ["/* Generated by companion/tools/make_logo.py – do not edit by hand */\n",
           '#include "logo_images.h"\n\n', c_image("logo_base", base)]
    header = ["/* Generated by companion/tools/make_logo.py – do not edit by hand */\n#pragma once\n\n#include \"lvgl.h\"\n\n",
              f"#define LOGO_SIZE {DEVICE_LOGO}\n\n", "extern const lv_image_dsc_t logo_base;\n"]
    for name, part, pos, pivot in parts:
        src.append(c_image(name, part))
        upper = name.upper()
        header.append(f"extern const lv_image_dsc_t {name};\n"
                      f"#define {upper}_X {pos[0]}\n#define {upper}_Y {pos[1]}\n"
                      f"#define {upper}_PIVOT_X {pivot[0]}\n#define {upper}_PIVOT_Y {pivot[1]}\n")
    src.append(c_image("logo_dial", dial))
    header.append(f"extern const lv_image_dsc_t logo_dial;\n#define LOGO_DIAL_X {box[0]}\n#define LOGO_DIAL_Y {box[1]}\n")
    (out / "logo_images.c").write_text("".join(src), encoding="utf-8")
    (out / "logo_images.h").write_text("".join(header), encoding="utf-8")


def main() -> None:
    logo = full_logo()
    write_app_assets(logo)
    write_device_images()
    print("logo written")


if __name__ == "__main__":
    main()
