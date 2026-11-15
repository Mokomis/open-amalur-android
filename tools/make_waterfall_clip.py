"""Generate the Veil portrait loop from a recording of the player's game."""

import argparse
from io import BytesIO
from pathlib import Path
import re
import subprocess

from PIL import Image, ImageEnhance

WIDTH = 256
HEIGHT = 320
FPS = 20
FRAMES = 100
BLEND_FRAMES = 16


def make_clip(source, output, start, crop):
    # Keep the character and HUD outside the chosen portrait crop.
    crop = crop or "w='min(iw,ih*4/5)':h='min(ih,iw*5/4)'"
    count = FRAMES + BLEND_FRAMES
    video = subprocess.check_output([
        "ffmpeg", "-nostdin", "-v", "error", "-ss", str(start), "-i", str(source), "-an",
        "-vf", f"fps={FPS},crop={crop},scale={WIDTH}:{HEIGHT}:flags=lanczos",
        "-frames:v", str(count), "-f", "rawvideo", "-pix_fmt", "rgb24", "-",
    ])
    length = WIDTH * HEIGHT * 3
    if len(video) != count * length:
        raise ValueError("Choose a recording with at least six seconds after the start point.")
    frames = [
        Image.frombytes("RGB", (WIDTH, HEIGHT), video[offset:offset + length])
        for offset in range(0, len(video), length)
    ]
    blocks = bytearray()
    for index, frame in enumerate(frames[:FRAMES]):
        if index < BLEND_FRAMES:
            weight = index / BLEND_FRAMES
            weight = weight * weight * (3 - 2 * weight)
            frame = Image.blend(frames[index + FRAMES], frame, weight)
        frame = ImageEnhance.Brightness(frame).enhance(0.75).convert("RGBA")
        with BytesIO() as encoded:
            frame.save(encoded, format="DDS", pixel_format="DXT5")
            texture = encoded.getvalue()
        # DXT5 DDS uses a 128-byte header and 16 bytes per 4×4 block.
        if len(texture) != 128 + WIDTH * HEIGHT or texture[84:88] != b"DXT5":
            raise ValueError("Pillow did not produce BC3 frames. Use Pillow 13.0.0.")
        blocks.extend(texture[128:])
    output.write_bytes(blocks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--start", type=float, default=1.5, help="start in seconds (default 1.5)")
    parser.add_argument("--crop", help="crop as width:height:left:top (default centered portrait)")
    args = parser.parse_args()
    if not args.start >= 0:
        parser.error("--start must be nonnegative")
    if args.crop and not re.fullmatch(r"[1-9][0-9]*:[1-9][0-9]*:[0-9]+:[0-9]+", args.crop):
        parser.error("--crop must be width:height:left:top in pixels")
    try:
        make_clip(args.capture, args.output, args.start, args.crop)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Could not create the waterfall loop. {error}\n")


if __name__ == "__main__":
    main()
