"""Pictures in the format LVGL on the device draws directly."""

from PIL import Image


def rgb565a8(img: Image.Image) -> bytes:
    """LVGL format RGB565A8: all colour values first (16 bit, little endian), then all alpha values.
    The icon stays cut out even when the key changes its colour while pressed."""
    raw = img.convert("RGBA").tobytes()
    rgb = bytearray()
    for i in range(0, len(raw), 4):
        value = (raw[i] >> 3) << 11 | (raw[i + 1] >> 2) << 5 | (raw[i + 2] >> 3)
        rgb += value.to_bytes(2, "little")
    return bytes(rgb) + raw[3::4]
