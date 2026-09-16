"""Bilder im Format, das LVGL auf dem Gerät direkt zeichnet."""

from PIL import Image


def rgb565a8(img: Image.Image) -> bytes:
    """LVGL-Format RGB565A8: erst alle Farbwerte (16 Bit, little endian), dann alle Alphawerte.
    Das Symbol bleibt so freigestellt, auch wenn die Taste beim Drücken ihre Farbe wechselt."""
    raw = img.convert("RGBA").tobytes()
    rgb = bytearray()
    for i in range(0, len(raw), 4):
        value = (raw[i] >> 3) << 11 | (raw[i + 1] >> 2) << 5 | (raw[i + 2] >> 3)
        rgb += value.to_bytes(2, "little")
    return bytes(rgb) + raw[3::4]
