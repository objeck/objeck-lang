#!/usr/bin/env python3
"""Draw docs/web/style/social-card.png, the 1200x630 image link previews show (Open Graph and Twitter cards).

    python tools/docs/make_social_card.py            write the card
    python tools/docs/make_social_card.py OUT.png    write it somewhere else

Needs Pillow and two fonts: Segoe UI and Consolas on Windows, DejaVu Sans and DejaVu Sans Mono elsewhere.
The colours are the site's dark theme tokens (docs/web/style/site.css). The card is a build product kept in the
tree so the site needs no tooling to serve it; run this again when the headline changes.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "docs", "web", "style", "social-card.png")
LOGO = os.path.join(ROOT, "docs", "web", "style", "objeck-logo.png")

BG, PANEL, LINE = "#0d1317", "#10181c", "#27343b"
TEXT, SOFT, MUTED, ACCENT = "#e7eef1", "#c6d2d8", "#9aabb4", "#5fd3c4"
KW, STR, TYPE, FN, CLS, COM = "#7cc4ff", "#9fdc8a", "#5fd3c4", "#ffa8c5", "#f2c56b", "#8a9aa3"


def font(names, size):
    for n in names:
        for d in ("C:/Windows/Fonts/", "/usr/share/fonts/truetype/dejavu/", "/Library/Fonts/", ""):
            try:
                return ImageFont.truetype(d + n, size)
            except OSError:
                pass
    sys.exit("no usable font among %s" % (names,))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else OUT
    im = Image.new("RGB", (1200, 630), BG)
    d = ImageDraw.Draw(im)
    sans_b = font(["segoeuib.ttf", "DejaVuSans-Bold.ttf"], 66)
    sans = font(["segoeui.ttf", "DejaVuSans.ttf"], 30)
    small = font(["segoeui.ttf", "DejaVuSans.ttf"], 26)
    mono = font(["consola.ttf", "DejaVuSansMono.ttf"], 25)

    logo = Image.open(LOGO).convert("RGBA")
    logo = logo.resize((int(logo.width * 56 / logo.height), 56))
    im.paste(logo, (64, 56), logo)

    # headline
    y = 170
    for line in ("An agentic", "language and", "runtime."):
        d.text((64, y), line, font=sans_b, fill=TEXT)
        y += 78
    d.text((64, y + 22), "Object-oriented. Functional. Fast.", font=sans, fill=MUTED)
    d.text((64, 548), "objeck.org", font=small, fill=ACCENT)

    # code panel
    x0, y0, x1, y1 = 600, 120, 1140, 510
    d.rounded_rectangle((x0, y0, x1, y1), radius=14, fill=PANEL, outline=LINE, width=2)
    lines = [
        [("#~", COM)],
        [("Greet a person.", COM)],
        [("@tool", ACCENT)],
        [("~#", COM)],
        [("function", KW), (" : ", SOFT), ("Greet", FN), ("(name : ", SOFT), ("String", TYPE), (")", SOFT)],
        [("    ~ ", SOFT), ("String", TYPE), (" {", SOFT)],
        [("  return ", KW), ('"Hello, {$name}"', STR), (";", SOFT)],
        [("}", SOFT)],
        [("", SOFT)],
        [("$ ", MUTED), ("obr --mcp tool.obe", TEXT)],
    ]
    ty = y0 + 28
    for parts in lines:
        tx = x0 + 30
        for text, col in parts:
            d.text((tx, ty), text, font=mono, fill=col)
            tx += d.textlength(text, font=mono)
        ty += 36
    im.save(out, optimize=True)
    print("wrote", out, os.path.getsize(out), "bytes")


main()
