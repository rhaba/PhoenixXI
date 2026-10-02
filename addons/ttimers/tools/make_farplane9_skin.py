"""
Draws the Farplane IX skin textures for tTimers' classic renderer.

    python tools/make_farplane9_skin.py

Writes resources/skins/classic/assets/{outline,bar}_farplane9.png (215 x 16, the same size as the
stock skins). The outline is drawn with the skin's BG color set to white, so its own colors show:
a charcoal slot with a misty FF9-style bevel frame and an ember hairline on top. The bar is
white, lighter at the top, and tinted at draw time by the skin's time colors.
"""

import os
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'resources', 'skins', 'classic', 'assets')
W, H = 215, 16


def hexc(h, a=255):
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def main():
    os.makedirs(OUT, exist_ok=True)

    outline = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(outline)
    d.rectangle([0, 0, W - 1, H - 1], fill=hexc('141317', 225))               # charcoal slot
    d.rectangle([0, 0, W - 1, H - 1], outline=hexc('cfdcd8', 120))            # misty frame
    d.line([1, 1, W - 2, 1], fill=hexc('ffffff', 34))                         # bevel highlight
    d.line([1, H - 2, W - 2, H - 2], fill=hexc('000000', 110))                # bevel shadow
    for x in range(2, W - 2):                                                 # ember hairline
        f = x / (W - 1)
        a = int(210 * max(0.0, min(1.0, f / 0.08, (1 - f) / 0.08)))
        outline.putpixel((x, 0), hexc('d2642a', a))
    d.line([16, 2, 16, H - 3], fill=hexc('cfdcd8', 45))                       # divider after the icon
    outline.save(os.path.join(OUT, 'outline_farplane9.png'))

    bar = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    for y in range(2, H - 2):
        if y == 2:
            v, a = 255, 255
        elif y == H - 3:
            v, a = 170, 230
        else:
            v, a = 230 - (y - 3) * 3, 215
        for x in range(1, W - 1):
            bar.putpixel((x, y), (v, v, v, a))
    bar.save(os.path.join(OUT, 'bar_farplane9.png'))
    print('wrote', OUT)


if __name__ == '__main__':
    main()
