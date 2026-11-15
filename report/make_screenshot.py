#!/usr/bin/env python3
"""Render REAL captured terminal output into a PNG image.

The content of every image is read verbatim from the saved run logs in
output_run/ (produced by run_tests.sh). Nothing is synthesized.

Usage: make_screenshot.py <logfile> <out.png> <title> [first_line] [last_line]
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

def _font(name):
    """DejaVu Sans Mono from the system (Linux) or, if absent (e.g. Windows),
    the identical copy bundled with matplotlib."""
    sys_path = os.path.join("/usr/share/fonts/truetype/dejavu", name)
    if os.path.exists(sys_path):
        return sys_path
    import matplotlib
    return os.path.join(matplotlib.get_data_path(), "fonts", "ttf", name)

FONT = _font("DejaVuSansMono.ttf")
FONT_B = _font("DejaVuSansMono-Bold.ttf")

def _note_font():
    """Font for the Vietnamese '... (lược bớt) ...' note lines this script adds.
    DejaVu Sans Mono lacks glyphs such as U+1EA7 (ầ) and U+1EE7 (ủ), so pick
    the first candidate that has them. Log lines themselves stay in FONT."""
    need = "đầủượớ"
    cands = [FONT, "C:/Windows/Fonts/consola.ttf",
             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
             _font("DejaVuSans.ttf")]
    try:
        from matplotlib import ft2font
    except ImportError:
        return FONT
    for p in cands:
        try:
            f = ft2font.FT2Font(p)
        except Exception:
            continue
        if all(f.get_char_index(ord(c)) for c in need):
            return p
    return FONT

NOTE_FONT = _note_font()

BG = (24, 24, 32)
FG = (220, 220, 220)
TITLE_BG = (50, 50, 62)
TITLE_FG = (235, 235, 235)
GREEN = (120, 220, 120)
YELLOW = (235, 210, 120)
CYAN = (120, 200, 235)
RED = (240, 130, 130)

def color_for(line: str):
    l = line.strip()
    if l.startswith("=====") or l.startswith("---"):
        return CYAN
    if "failed" in l or "Invalid" in l or "Cannot" in l:
        return RED
    if "has finished" in l or "DMA completed" in l or "exit=0" in l:
        return GREEN
    if l.startswith("Time slot"):
        return YELLOW
    return FG

def main():
    log, out, title = sys.argv[1], sys.argv[2], sys.argv[3]
    first = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    last = int(sys.argv[5]) if len(sys.argv) > 5 else 10**9

    with open(log, encoding="utf-8", errors="replace") as f:
        all_lines = f.read().splitlines()
    total = len(all_lines)
    lines = all_lines[first - 1:last]
    truncated_head = first > 1
    truncated_tail = last < total

    shown = []
    if truncated_head:
        shown.append(f"... (dòng 1..{first-1} lược bớt) ...")
    shown += lines
    if truncated_tail:
        shown.append(f"... (còn {total-last} dòng, xem file log đầy đủ) ...")

    fsize = 15
    font = ImageFont.truetype(FONT, fsize)
    font_b = ImageFont.truetype(FONT_B, fsize)
    font_note = ImageFont.truetype(NOTE_FONT, fsize)
    lh = fsize + 6
    pad = 14
    titlebar = 34

    maxchars = max([len(s) for s in shown] + [len(title) + 10, 60])
    w = min(pad * 2 + int(maxchars * fsize * 0.62), 1500)
    h = titlebar + pad * 2 + lh * len(shown)

    img = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(img)

    # terminal title bar
    d.rectangle([0, 0, w, titlebar], fill=TITLE_BG)
    for i, c in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]):
        d.ellipse([12 + i * 22, 10, 26 + i * 22, 24], fill=c)
    d.text((86, 8), title, font=font_b, fill=TITLE_FG)

    y = titlebar + pad
    for s in shown:
        if s.startswith("... ("):
            d.text((pad, y), s, font=font_note, fill=(150, 150, 160))
        else:
            d.text((pad, y), s, font=font, fill=color_for(s))
        y += lh

    img.save(out)
    print(f"saved {out} ({w}x{h}, {len(shown)} lines from {log})")

if __name__ == "__main__":
    main()
