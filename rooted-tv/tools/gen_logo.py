#!/usr/bin/env python3
"""Generate Rooted TV's logo files from the one drawing below.

The logo is a screen with a sprout for its aerial, and the pixel it draws. This writes the tile
(docs/logo/rootedtv.svg, assets/favicon.svg), the marks the TV client draws (assets/icons/), and
the PNG icons of the TV apps, the phone dashboard's install and the docs, rendered with
rsvg-convert. The pages that inline the mark (web/ui.html and the pages under
assets/google-tv/pages/) carry a copy of assets/icons/logo.svg by hand.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT.parent / "docs"

# On a 512 grid. The screen and stem are strokes, the leaves and the pixel fills.
SCREEN = '<rect x="108" y="200" width="296" height="200" rx="48" fill="none" stroke="{ink}" stroke-width="38"/>'
STEM = '<path d="M256 200 V150" fill="none" stroke="{ink}" stroke-width="30" stroke-linecap="round"/>'
LEAVES = ('<path d="M252 156 C214 158 186 134 180 96 C220 94 248 118 252 156 Z '
          'M260 146 C268 102 300 76 344 78 C344 122 312 148 260 146 Z" fill="{leaf}"/>')
PIXEL = '<circle cx="256" cy="300" r="26" fill="{pixel}"/>'
# The glyph's bounds with a little room: the marks' view box, and a square one for the splash,
# whose pixel src/ui/splash.cpp draws itself (kDotX, kDotY and kDotRadius are fractions of it).
TIGHT = "84 72 344 352"
SQUARE = "80 72 352 352"
TILE = ('<defs><linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#5df08a"/>'
        '<stop offset="1" stop-color="#1fb653"/></linearGradient></defs>')
ROUNDED = ('<rect width="512" height="512" rx="112" fill="url(#bg)"/>'
           '<rect x="1.5" y="1.5" width="509" height="509" rx="110.5" fill="none" stroke="#fff" '
           'stroke-opacity=".25" stroke-width="3"/>')
SVG = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="{box}">{body}</svg>\n'


def glyph(ink, leaf=None, pixel=None):
    return (SCREEN + STEM + LEAVES + ("" if pixel == "" else PIXEL)).format(
        ink=ink, leaf=leaf or ink, pixel=pixel or ink)


def tile():
    return SVG.format(box="0 0 512 512", body=(
        "<!-- Rooted TV: a screen with a sprout for its aerial, and the pixel it draws. -->"
        + TILE + ROUNDED + glyph("#000")))


def maskable():
    # A launcher crops a maskable icon to a circle as small as 80% of it: the glyph stays inside.
    return SVG.format(box="0 0 512 512", body=(
        TILE + '<rect width="512" height="512" fill="url(#bg)"/>'
        '<g transform="translate(256 256) scale(.7) translate(-256 -248)">' + glyph("#000") + "</g>"))


def render(svg, size, out):
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), "-o", str(out)],
                   input=svg.encode(), check=True)


def main():
    files = {
        DOCS / "logo" / "rootedtv.svg": tile(),
        ROOT / "assets" / "favicon.svg": tile(),
        ROOT / "assets" / "icons" / "logo.svg": (
            '<svg id="mark" viewBox="%s" role="img" aria-label="Rooted TV" translate="no">%s</svg>\n'
            % (TIGHT, glyph("currentColor"))),
        ROOT / "assets" / "icons" / "mark.svg": SVG.format(box=SQUARE, body=glyph("#fff", pixel="")),
        ROOT / "assets" / "icons" / "splash-mark.svg": SVG.format(
            box=TIGHT, body=glyph("#ece7df", leaf="#30de61", pixel="#30de61")),
    }
    for path, text in files.items():
        path.write_text(text)
    render(tile(), 512, DOCS / "logo" / "rootedtv-512.png")
    render(tile(), 80, ROOT / "assets" / "icon.png")
    for app in ("dashboard-app", "home-app", "splash-app"):
        render(tile(), 80, ROOT / "assets" / app / "assets" / "icon80.png")
        render(tile(), 130, ROOT / "assets" / app / "assets" / "icon130.png")
    render(tile(), 192, ROOT / "assets" / "pwa" / "icon-192.png")
    render(tile(), 512, ROOT / "assets" / "pwa" / "icon-512.png")
    render(maskable(), 512, ROOT / "assets" / "pwa" / "icon-maskable-512.png")


if __name__ == "__main__":
    main()
