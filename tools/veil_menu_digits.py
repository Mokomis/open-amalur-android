"""Align the ten decimal digits in each Veil menu font to its zero glyph."""

from math import floor
from struct import unpack_from

FONTS = ("savapro_medium", "byngve_bold")
ATLAS_WIDTH = 512
HEADER = 44
FONT_TABLE = 0x4C


def atlas_name(font):
    return font + font + "_p00_00.ntx"


def lining_digits(metrics, atlas):
    """Resample only digit cells vertically, retaining bearings, advance and kerning."""
    if atlas[:8] != b"FORK\x02\x01\x02\x00" or unpack_from(">III", atlas, 16) != (
        HEADER,
        ATLAS_WIDTH**2,
        ATLAS_WIDTH**2,
    ):
        raise ValueError("Unexpected Veil font atlas")
    pixels = memoryview(atlas)[HEADER : HEADER + ATLAS_WIDTH**2]
    if len(pixels) != ATLAS_WIDTH**2:
        raise ValueError("Truncated font atlas")
    output = bytearray(atlas)
    (page,) = unpack_from("<h", metrics, FONT_TABLE + 0xA48)
    if page < 0:
        raise ValueError("Font is missing its Latin glyph page")
    for code in range(ord("0"), ord("9") + 1):
        (index,) = unpack_from("<h", metrics, FONT_TABLE + 0x48 + 512 * page + 2 * code)
        if index < 0:
            raise ValueError("Missing menu digit")
        _, bearing, x, right, y, bottom, *_ = unpack_from(
            "<9h", metrics, FONT_TABLE + 0xA9C + 18 * index
        )
        if not (0 <= x < right <= ATLAS_WIDTH and 0 <= y < bottom <= ATLAS_WIDTH):
            raise ValueError("Invalid digit cell")
        rows = [
            pixels[row * ATLAS_WIDTH + x : row * ATLAS_WIDTH + right]
            for row in range(y, bottom)
        ]
        ink = [row for row, samples in enumerate(rows) if max(samples) > 128]
        if not ink:
            raise ValueError("Empty menu digit")
        top, end = ink[0], ink[-1] + 1
        if code == ord("0"):
            height = end - top
            baseline = end - bearing / 4
        target_end = baseline + bearing / 4
        target_top = target_end - height
        if target_top < 0 or target_end > len(rows):
            raise ValueError("Lining digit would exceed its atlas cell")
        if top == target_top and end == target_end:
            continue
        # Resample the full distance field, including its antialiasing margin.
        for row in range(len(rows)):
            source = top + (row + 0.5 - target_top) * (end - top) / height - 0.5
            source = min(max(source, 0), len(rows) - 1)
            low = floor(source)
            fraction = source - low
            start = HEADER + (y + row) * ATLAS_WIDTH + x
            output[start : start + right - x] = bytes(
                round(a + (b - a) * fraction)
                for a, b in zip(rows[low], rows[min(low + 1, len(rows) - 1)])
            )
    return bytes(output)
