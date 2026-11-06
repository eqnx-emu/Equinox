"""Render the Equinox logo PNG/JPG assets from the shapes in equinox.svg.

Run from the repository root:  python art/logo/render_logo.py
Needs Pillow. Shapes are drawn on a 100-unit grid (same as equinox.svg)
at 8x supersampling and downscaled, so edges stay smooth.
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # art/logo -> repo
IMG = os.path.join(ROOT, "resources", "img")
FONT = os.path.join(ROOT, "resources", "font", "onest_semibold.ttf")

DUSK = (22, 26, 51)
DUSK_GLOW = (35, 40, 88)
SUN = (244, 184, 96)
MOON = (157, 184, 255)
SS = 8


def draw_mark(size, detail=True):
    """The mark on a transparent square of `size` px."""
    big = size * SS
    k = big / 100.0
    im = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    def p(x, y):
        return (x * k, y * k)

    # Sun rays (round caps).
    w = 5 * k
    for x1, y1, x2, y2 in [(14, 50, 4, 50), (21.7, 27, 14.6, 19.9),
                           (21.7, 73, 14.6, 80.1), (47, 10, 47, 2),
                           (47, 90, 47, 98)]:
        d.line([p(x1, y1), p(x2, y2)], fill=SUN, width=round(w))
        for x, y in ((x1, y1), (x2, y2)):
            cx, cy = p(x, y)
            d.ellipse([cx - w / 2, cy - w / 2, cx + w / 2, cy + w / 2], fill=SUN)

    # Sun half (left) and moon half (right), radius 30, 6-unit gap.
    d.pieslice([p(17, 20), p(77, 80)], 90, 270, fill=SUN)
    d.pieslice([p(23, 20), p(83, 80)], 270, 90, fill=MOON)

    if detail:
        crater = Image.new("RGBA", im.size, (0, 0, 0, 0))
        cd = ImageDraw.Draw(crater)
        for cx, cy, r, a in [(62, 38, 4, 0.35), (66, 60, 6, 0.25)]:
            cd.ellipse([p(cx - r, cy - r), p(cx + r, cy + r)],
                       fill=DUSK + (round(255 * a),))
        im = Image.alpha_composite(im, crater)
        d = ImageDraw.Draw(im)
        # Four-point star and a dot.
        sx, sy = 88, 27.6
        star = []
        for i in range(8):
            ang = math.pi / 2 * (i // 2) + (math.pi / 4 if i % 2 else 0)
            r = 5.6 if i % 2 == 0 else 2.26
            star.append(p(sx + r * math.cos(ang), sy + r * math.sin(ang)))
        d.polygon(star, fill=MOON)
        d.ellipse([p(89.8, 69.8), p(94.2, 74.2)], fill=MOON)

    return im.resize((size, size), Image.LANCZOS)


def app_icon(size=256):
    """Square console icon: dusk tile with a soft glow and the mark."""
    big = size * SS
    tile = Image.new("RGB", (big, big), DUSK)
    glow = Image.new("L", (big, big), 0)
    gd = ImageDraw.Draw(glow)
    cx, cy, rmax = big * 0.5, big * 0.35, big * 0.7
    steps = 64
    for i in range(steps, 0, -1):
        r = rmax * i / steps
        gd.ellipse([cx - r, cy - r, cx + r, cy + r], fill=round(255 * (1 - i / steps)))
    tile.paste(Image.new("RGB", (big, big), DUSK_GLOW), (0, 0), glow)
    tile = tile.resize((size, size), Image.LANCZOS)
    mark = draw_mark(round(size * 0.75))
    off = (size - mark.width) // 2
    tile.paste(mark, (off, off), mark)
    return tile


def wordmark(text_color, width=848, height=208):
    """Mark plus "Equinox" for the About tab, transparent background."""
    out = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    mark = draw_mark(176)
    out.alpha_composite(mark, (8, (height - 176) // 2))
    font = ImageFont.truetype(FONT, 132)
    d = ImageDraw.Draw(out)
    text = "Equinox"
    box = d.textbbox((0, 0), text, font=font)
    x = 8 + 176 + 36
    y = (height - (box[3] - box[1])) // 2 - box[1]
    d.text((x, y), text, font=font, fill=text_color)
    return out


if __name__ == "__main__":
    app_icon().save(os.path.join(IMG, "moonlight_icon.jpg"), quality=95)
    draw_mark(96, detail=False).save(os.path.join(IMG, "moonlight_96.png"))
    wordmark((238, 240, 250)).save(os.path.join(IMG, "equinox_logo_dark.png"))
    wordmark(DUSK).save(os.path.join(IMG, "equinox_logo_light.png"))
    print("ok")
