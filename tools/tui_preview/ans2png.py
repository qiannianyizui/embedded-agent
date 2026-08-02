#!/usr/bin/env python3
"""Render an FTXUI ANSI screen capture into a PNG screenshot.

Usage:
    python3 tools/tui_preview/ans2png.py <in.ans> <out.png> [--font-size N]

The input is the output of `screen.ToString()` from an FTXUI offline render
(see src/tui/preview/tui_preview.cpp). Parses 24-bit SGR sequences and paints
each cell with DejaVu Sans Mono, honoring bold/dim/inverted styles.
"""

import argparse
import re
import unicodedata

from PIL import Image, ImageDraw, ImageFont


DEFAULT_FG = (232, 234, 242)   # theme.text
DEFAULT_BG = (13, 17, 23)      # theme.bg


def parse_ansi(text):
    """Yield (fg, bg, bold, dim, inverted, char) per cell."""
    fg = DEFAULT_FG
    bg = DEFAULT_BG
    bold = False
    dim = False
    inverted = False
    rows = []
    row = []

    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "\x1b":
            m = re.match(r"\x1b\[([0-9;]*)m", text[i:])
            if m:
                params = m.group(1).split(";")
                j = 0
                while j < len(params):
                    p = params[j]
                    if p in ("", "0"):
                        fg, bg, bold, dim, inverted = DEFAULT_FG, DEFAULT_BG, False, False, False
                    elif p == "1":
                        bold = True
                    elif p == "2":
                        dim = True
                    elif p == "22":
                        bold = dim = False
                    elif p == "7":
                        inverted = True
                    elif p == "27":
                        inverted = False
                    elif p == "39":
                        fg = DEFAULT_FG
                    elif p == "49":
                        bg = DEFAULT_BG
                    elif p == "38" and j + 3 < len(params) and params[j + 1] == "2":
                        fg = tuple(int(x) for x in params[j + 2:j + 5])
                        j += 4
                    elif p == "48" and j + 3 < len(params) and params[j + 1] == "2":
                        bg = tuple(int(x) for x in params[j + 2:j + 5])
                        j += 4
                    j += 1
                i += m.end()
                continue
            # Unknown escape: skip to end of sequence
            m2 = re.match(r"\x1b\[[0-9;?]*[A-Za-z]", text[i:])
            i += m2.end() if m2 else 1
            continue
        if ch == "\n":
            rows.append(row)
            row = []
            i += 1
            continue
        if ch == "\r":
            i += 1
            continue
        row.append((fg, bg, bold, dim, inverted, ch))
        i += 1
    if row:
        rows.append(row)
    return rows


def cell_width(ch):
    return 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1


def blend(fg, bg, dim):
    if not dim:
        return fg
    return tuple(round(bg[k] * 0.55 + fg[k] * 0.45) for k in range(3))


def render(rows, font_path, bold_path, font_size, out_path):
    font = ImageFont.truetype(font_path, font_size)
    bold_font = ImageFont.truetype(bold_path, font_size)
    ascent, descent = font.getmetrics()
    cell_w = round(font.getlength("M"))
    line_h = font_size + max(descent, 2)
    width = max((len(r) for r in rows), default=1)
    height = len(rows)

    img = Image.new("RGB", (width * cell_w, height * line_h), DEFAULT_BG)
    draw = ImageDraw.Draw(img)

    for y, row in enumerate(rows):
        x = 0
        for (fg, bg, bold, dim, inverted, ch) in row:
            cw = cell_width(ch)
            if x + cw * cell_w > img.width:
                break
            f, b = fg, bg
            if inverted:
                f, b = b, f
            if b != DEFAULT_BG:
                draw.rectangle((x, y * line_h, x + cw * cell_w, (y + 1) * line_h), fill=b)
            color = blend(f, b, dim)
            fnt = bold_font if bold else font
            draw.text((x, y * line_h + ascent), ch, font=fnt, fill=color, anchor="ls")
            x += cw * cell_w

    img.save(out_path)
    print(f"wrote {out_path} ({img.width}x{img.height})")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--font-size", type=int, default=20)
    args = parser.parse_args()

    with open(args.input, "rb") as fh:
        text = fh.read().decode("utf-8", errors="replace")

    rows = parse_ansi(text)
    render(
        rows,
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
        args.font_size,
        args.output,
    )


if __name__ == "__main__":
    main()
