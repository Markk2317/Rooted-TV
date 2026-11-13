#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["resvg-py"]
# ///
"""Generate rooted-tv/assets/screensavers/weather-<icon>.png, the pictures the Photos screen saver
draws beside the temperature, as Google TV's ambient mode does: a light cloud, a yellow sun, a
pale moon, blue rain. The names are service::weatherIcon()'s (src/service/weather.cpp). Drawn on
a 48-unit square and rendered at 96 px, twice the size the screen saver shows them at.
"""
from pathlib import Path

import resvg_py

OUTPUT = Path(__file__).resolve().parents[1] / "assets" / "screensavers"
RENDER_PX = 96

DEFS = """<defs>
<linearGradient id="cloud" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#FFFFFF"/><stop offset="1" stop-color="#C4C7CB"/></linearGradient>
<linearGradient id="dark" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#C4C7CB"/><stop offset="1" stop-color="#7A7F85"/></linearGradient>
<linearGradient id="back" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#C4C7CB"/><stop offset="1" stop-color="#8E9399"/></linearGradient>
</defs>"""


def cloud(dx=0, dy=0, scale=1.0, fill="url(#cloud)"):
    # Two puffs over a rounded base, 34 units wide.
    return (f'<g transform="translate({dx} {dy}) scale({scale})" fill="{fill}">'
            '<circle cx="17" cy="25" r="8"/><circle cx="28" cy="21" r="10.5"/>'
            '<rect x="7" y="24" width="34" height="13" rx="6.5"/></g>')


def sun(cx, cy, r, rays=True):
    parts = [f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="#FDD663"/>']
    if rays:
        for i in range(8):
            parts.append(f'<rect x="{cx - 1.5}" y="{cy - r - 7}" width="3" height="4.6" rx="1.5" '
                         f'fill="#FDD663" transform="rotate({i * 45} {cx} {cy})"/>')
    return "".join(parts)


def moon(cx, cy, r):
    return (f'<mask id="bite"><rect width="48" height="48" fill="white"/>'
            f'<circle cx="{cx + r * .55}" cy="{cy - r * .45}" r="{r * .82}" fill="black"/></mask>'
            f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="#F3E7B3" mask="url(#bite)"/>')


def drops(color, r, rows=(40, 43, 40)):
    return "".join(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>'
                   for x, y in zip((16, 24, 32), rows))


def rain():
    return "".join(f'<path d="M{x} {y - 5} q3.6 4.6 3.6 6.8 a3.6 3.6 0 0 1 -7.2 0 q0 -2.2 3.6 -6.8z" '
                   f'fill="#4A86E8"/>' for x, y in ((16, 41), (24, 44), (32, 41)))


ICONS = {
    "clear-day": sun(24, 24, 9.5),
    "clear-night": moon(24, 24, 13),
    "partly-day": sun(31, 15, 7) + cloud(-2, 6, .95),
    "partly-night": moon(31, 15, 9) + cloud(-2, 6, .95),
    "cloudy": cloud(8, -4, .78, "url(#back)") + cloud(-2, 5, .95),
    "fog": cloud(0, -3) + '<rect x="9" y="38" width="30" height="3" rx="1.5" fill="#C4C7CB"/>'
                          '<rect x="13" y="43.5" width="22" height="3" rx="1.5" fill="#C4C7CB"/>',
    "drizzle": cloud(0, -5) + drops("#8AB4F8", 2.2),
    "rain": cloud(0, -6) + rain(),
    "snow": cloud(0, -6) + "".join(
        f'<circle cx="{x}" cy="{y}" r="2.8" fill="#FFFFFF" stroke="#AECBFA" stroke-width=".8"/>'
        for x, y in ((16, 39), (24, 43), (32, 39))),
    "storm": cloud(0, -6, 1, "url(#dark)") +
             '<path d="M25 30 l-6 9 h5 l-3 8 l9 -11 h-5 l3 -6z" fill="#FDD663"/>',
}


def main():
    for name, body in ICONS.items():
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48" width="48" height="48">'
               f'{DEFS}{body}</svg>')
        png = bytes(resvg_py.svg_to_bytes(svg_string=svg, width=RENDER_PX, height=RENDER_PX))
        (OUTPUT / f"weather-{name}.png").write_bytes(png)
        print(f"weather-{name}.png {len(png)} bytes")


if __name__ == "__main__":
    main()
