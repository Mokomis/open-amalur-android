"""Render a replacement typeface within the existing Veil glyph bounds."""
import argparse
from pathlib import Path
import struct

from fontTools.ttLib import TTFont
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import distance_transform_edt

SCALE = 8
GAME_GLYPH = struct.Struct("<9h")
GAME_TABLE = 0x4C
GAME_GLYPHS = GAME_TABLE + 0xA9C
GAME_DIMENSIONS = (512, 512)
PROTOTYPE_GLYPH = struct.Struct("<4H2hf")
WHITESPACE = {0x20, 0xA0, 0xAD}


def ink_bounds(image):
    return image.point(lambda alpha: 255 if alpha > 128 else 0).getbbox()


def glyph_codes(metrics):
    pages = struct.unpack_from("<38h", metrics, GAME_TABLE + 0xA48)
    for high, page in enumerate(pages):
        if page < 0:
            continue
        offset = GAME_TABLE + 0x48 + page * 512
        for low, index in enumerate(struct.unpack_from("<256h", metrics, offset)):
            if index >= 0:
                yield index, (high << 8) | low


def game_glyph(metrics, index):
    _, _, left, right, top, bottom, *_ = GAME_GLYPH.unpack_from(metrics, GAME_GLYPHS + index * GAME_GLYPH.size)
    return left, top, right, bottom


def raster(face, character):
    left, top, right, bottom = face.getbbox(character, anchor="ls")
    image = Image.new("L", (right - left + 2, bottom - top + 2))
    ImageDraw.Draw(image).text((1 - left, 1 - top), character, font=face, fill=255, anchor="ls")
    bounds = ink_bounds(image)
    return image.crop(bounds) if bounds is not None else None


def fitted_field(glyph, bounds, cell_size, slope, max_ink):
    if glyph is None:
        raise ValueError("The replacement font has no ink for a visible glyph")
    left, top, right, bottom = bounds
    width, height = cell_size
    scale = min((right - left) * SCALE / glyph.width, (bottom - top) * SCALE / glyph.height)
    fitted = glyph.resize((max(1, round(glyph.width * scale)), max(1, round(glyph.height * scale))),
                          Image.Resampling.LANCZOS)
    mask = Image.new("L", (width * SCALE, height * SCALE))
    x = (left + right) * SCALE // 2 - fitted.width // 2
    mask.paste(fitted, (x, bottom * SCALE - fitted.height))
    interior = np.asarray(mask) >= 128
    distance = (distance_transform_edt(interior) - distance_transform_edt(~interior)) / SCALE
    field = Image.fromarray(np.clip(128 + distance * slope, 0, max_ink).astype(np.uint8))
    result = field.resize(cell_size, Image.Resampling.BOX)
    # Keep the antialiasing ramp around the glyph. Limit visible ink to the
    # previously validated rectangle, including at fractional raster scales.
    samples = np.array(result)
    samples[:top, :] = np.minimum(samples[:top, :], 128)
    samples[bottom:, :] = np.minimum(samples[bottom:, :], 128)
    samples[:, :left] = np.minimum(samples[:, :left], 128)
    samples[:, right:] = np.minimum(samples[:, right:], 128)
    result = Image.fromarray(samples)
    actual = ink_bounds(result)
    if actual is None or not (left <= actual[0] < actual[2] <= right
                              and top <= actual[1] < actual[3] <= bottom):
        raise ValueError(f"Visible glyph escaped or disappeared inside {bounds}")
    return result


def game_atlas(path):
    data = path.read_bytes()
    if data[:8] != b"FORK\x02\x01\x02\x00" or struct.unpack_from(">III", data, 16) != (44, 262144, 262144):
        raise ValueError(f"Unexpected font atlas format in {path.name}")
    return data, Image.frombytes("L", GAME_DIMENSIONS, data[44:44 + 262144])


def build_game(face, cmap, reference, output, slot, slope, max_ink):
    name = slot * 2 + "_p00_00.ntx"
    metrics = (reference / f"current-{slot}.font").read_bytes()
    stock_metrics = (reference / f"retail-{slot}.font").read_bytes()
    previous_data, previous = game_atlas(reference / f"current-{name}")
    _, stock = game_atlas(reference / f"retail-{name}")
    result = Image.new("L", GAME_DIMENSIONS)
    fill = 0
    for index, point in glyph_codes(metrics):
        cell = game_glyph(metrics, index)
        original = previous.crop(cell)
        bounds = ink_bounds(original)
        if bounds is None or point in WHITESPACE:
            continue
        glyph = raster(face, chr(point)) if point in cmap else None
        if glyph is None:
            # Player-supplied retail symbols remain available for characters
            # that were absent from the replacement font.
            stock_glyph = stock.crop(game_glyph(stock_metrics, index))
            fallback_bounds = ink_bounds(stock_glyph)
            glyph = stock_glyph.point(lambda alpha: 255 if alpha > 128 else 0).crop(fallback_bounds) if fallback_bounds else None
        result.paste(fitted_field(glyph, bounds, original.size, slope, max_ink), cell[:2])
        fill += 1
    (output / name).write_bytes(previous_data[:44] + result.tobytes() + previous_data[44 + 262144:])
    print(f"{slot}: fitted {fill} visible glyphs; existing metrics and advances retained")


def build_prototype(face, path, output):
    data = path.read_bytes()
    if data[:8] != b"OAUI0002":
        raise ValueError("Unexpected prototype UI version")
    sprite_w, sprite_h = struct.unpack_from("<II", data, 8)
    sprites_end = 16 + sprite_w * sprite_h * 4
    count, = struct.unpack_from("<I", data, sprites_end)
    atlas_header = sprites_end + 4 + count * 8
    width, height = struct.unpack_from("<II", data, atlas_header)
    alpha_at = atlas_header + 8
    glyphs_at = alpha_at + width * height
    previous = Image.frombytes("L", (width, height), data[alpha_at:glyphs_at])
    result = Image.new("L", (width, height))
    for style in range(2):
        cells = glyphs_at + style * (12 + 95 * PROTOTYPE_GLYPH.size) + 12
        for index in range(95):
            x, y, w, h, *_ = PROTOTYPE_GLYPH.unpack_from(data, cells + index * PROTOTYPE_GLYPH.size)
            if not w or not h:
                continue
            bounds = ink_bounds(previous.crop((x, y, x + w, y + h)))
            if bounds is not None:
                glyph = raster(face, chr(index + 32))
                result.paste(fitted_field(glyph, bounds, (w, h), 16, 255), (x, y))
    (output / "oa_prototype_ui.bin").write_bytes(data[:alpha_at] + result.tobytes() + data[glyphs_at:])
    print("Pause overlay: regenerated both glyph sets with existing positions and advances")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("face", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--prototype", type=Path)
    args = parser.parse_args()
    args.output.mkdir()
    face = ImageFont.truetype(str(args.face), 32 * SCALE)
    if hasattr(face, "get_variation_axes"):
        try:
            axes = face.get_variation_axes()
        except OSError:
            axes = []
        if axes:
            face.set_variation_by_axes([axis["default"] for axis in axes])
    with TTFont(args.face) as font:
        cmap = font.getBestCmap()
    build_game(face, cmap, args.reference, args.output, "savapro_medium", 14, 156)
    build_game(face, cmap, args.reference, args.output, "byngve_bold", 38, 218)
    if args.prototype is not None:
        build_prototype(face, args.prototype, args.output)


if __name__ == "__main__":
    main()
