#!/usr/bin/env python3
"""make_bezel_png.py — convert a raw CRT photo into a Tubelight-ready bezel PNG.

Usage:
    python make_bezel_png.py <input.jpg> <output.png> [--rect x1,y1,x2,y2]
                              [--size W,H] [--feather PX] [--rotate DEG]

The output PNG has:
  - The full monitor casing visible as opaque pixels (alpha=255).
  - The screen area cut out with alpha=0, so Tubelight's renderer draws
    the CRT pipeline output there.
  - The image is resized to (W,H) — default 1280x960 (4:3).
  - The transition between bezel and cutout is soft-edged ("feathered")
    by PX pixels on each side so the screen doesn't have a hard edge.

If --rect isn't given the script tries to auto-detect the screen area
(largest dark connected blob, threshold luminance < 30). For arbitrary
photos that detection is rough; pass --rect explicitly for control.
"""

from __future__ import annotations
import argparse
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError:
    print("ERROR: pillow not installed. pip install pillow", file=sys.stderr)
    sys.exit(1)

def parse_rect(s: str):
    parts = s.split(",")
    if len(parts) != 4:
        raise ValueError("rect must be x1,y1,x2,y2")
    return tuple(int(p) for p in parts)

def parse_size(s: str):
    w, h = s.split(",")
    return int(w), int(h)

def auto_detect_screen(img: Image.Image, threshold: int = 30):
    """Find the largest dark blob (the screen). Returns (x1,y1,x2,y2) in
    image coordinates or None if nothing found. Uses very simple flood-
    fill from the centre — works for monitors with a roughly centred
    screen and noticeably darker tube than bezel."""
    gray = img.convert("L")
    w, h = gray.size
    # Sample a horizontal line across the centre, find the longest run of dark.
    cy = h // 2
    line = [gray.getpixel((x, cy)) for x in range(w)]
    runs = []
    cur_start = None
    for x, v in enumerate(line):
        if v < threshold:
            if cur_start is None: cur_start = x
        else:
            if cur_start is not None:
                runs.append((cur_start, x - 1)); cur_start = None
    if cur_start is not None: runs.append((cur_start, w - 1))
    if not runs: return None
    x1, x2 = max(runs, key=lambda r: r[1] - r[0])
    # Same trick vertically through the midpoint of the horizontal run.
    cx = (x1 + x2) // 2
    col = [gray.getpixel((cx, y)) for y in range(h)]
    vruns = []
    cur_start = None
    for y, v in enumerate(col):
        if v < threshold:
            if cur_start is None: cur_start = y
        else:
            if cur_start is not None:
                vruns.append((cur_start, y - 1)); cur_start = None
    if cur_start is not None: vruns.append((cur_start, h - 1))
    if not vruns: return None
    y1, y2 = max(vruns, key=lambda r: r[1] - r[0])
    return (x1, y1, x2, y2)

def main():
    p = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("input")
    p.add_argument("output")
    p.add_argument("--rect", help="screen area in source pixels x1,y1,x2,y2")
    p.add_argument("--size", default="1280,960", help="output WxH (default 1280x960)")
    p.add_argument("--feather", type=int, default=4,
                   help="soft-edge in output pixels (default 4)")
    p.add_argument("--rotate", type=float, default=0,
                   help="rotate input by this many degrees first")
    p.add_argument("--corner-radius", type=float, default=0.03,
                   help="rounded-corner radius as fraction of min(out_w,out_h)")
    args = p.parse_args()

    img = Image.open(args.input).convert("RGB")
    if args.rotate:
        img = img.rotate(args.rotate, expand=True, resample=Image.BICUBIC)

    if args.rect:
        rect = parse_rect(args.rect)
    else:
        rect = auto_detect_screen(img)
        if rect is None:
            print("could not auto-detect screen; pass --rect", file=sys.stderr)
            sys.exit(2)
        print(f"detected screen rect (pixel coords): {rect}")

    out_w, out_h = parse_size(args.size)
    sx1, sy1, sx2, sy2 = rect
    src_w, src_h = img.size

    # Resize the photo to the output dimensions, mapping the source rect
    # to a "screen rect" in the output that takes up ~70-80 % of width
    # and ~65-75 % of height (matches a typical front-facing CRT photo).
    img_r = img.resize((out_w, out_h), Image.BICUBIC)

    # Map source rect → output rect (linear scale).
    ox1 = int(sx1 * out_w / src_w)
    oy1 = int(sy1 * out_h / src_h)
    ox2 = int(sx2 * out_w / src_w)
    oy2 = int(sy2 * out_h / src_h)
    print(f"output screen rect: ({ox1},{oy1}) to ({ox2},{oy2}) inside {out_w}x{out_h}")

    # Build alpha mask. Start fully opaque, punch out the screen rect.
    alpha = Image.new("L", (out_w, out_h), 255)
    draw = ImageDraw.Draw(alpha)
    r = int(min(out_w, out_h) * args.corner_radius)
    draw.rounded_rectangle([ox1, oy1, ox2, oy2], radius=r, fill=0)
    if args.feather > 0:
        alpha = alpha.filter(ImageFilter.GaussianBlur(args.feather))

    rgba = img_r.convert("RGBA")
    rgba.putalpha(alpha)
    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    rgba.save(args.output, "PNG")
    print(f"wrote {args.output}  ({out_w}x{out_h}, RGBA)")

if __name__ == "__main__":
    main()
