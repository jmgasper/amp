#!/usr/bin/env python3
"""Prepare the MiniDisc pictures Amp carries in its resources from the artwork in
resources/branding/source.

    python3 tools/make-images.py

  minidisc.png        the colour cartridge of the MiniDisc view and the display: the grey
                      backdrop of the drawing becomes transparent, its shadow stays
  minidisc-glyph.png  the one-colour cartridge of the sidebar and the status bar: only its
                      alpha channel matters, the views tint it
"""
import os

from PIL import Image, ImageChops, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, 'resources', 'branding', 'source')
OUT = os.path.join(ROOT, 'resources', 'images')


def square(image, margin):
    """Crop to what is visible and centre it in a square with `margin` (a fraction) around."""
    box = image.getchannel('A').point(lambda v: 255 if v > 6 else 0).getbbox()
    image = image.crop(box)
    side = int(round(max(image.size) * (1 + 2 * margin)))
    result = Image.new('RGBA', (side, side), (0, 0, 0, 0))
    result.paste(image, ((side - image.size[0]) // 2, (side - image.size[1]) // 2))
    return result


def shrink(image, size):
    """Scale down with premultiplied alpha, so transparent pixels do not bleed their colour."""
    r, g, b, a = image.split()
    premultiplied = Image.merge('RGBA', [ImageChops.multiply(c, a) for c in (r, g, b)] + [a])
    small = premultiplied.resize((size, size), Image.LANCZOS)
    r, g, b, a = small.split()
    pixels = list(zip(r.getdata(), g.getdata(), b.getdata(), a.getdata()))
    out = [(min(255, pr * 255 // pa), min(255, pg * 255 // pa), min(255, pb * 255 // pa), pa) if pa else (0, 0, 0, 0)
           for pr, pg, pb, pa in pixels]
    result = Image.new('RGBA', (size, size))
    result.putdata(out)
    return result


def cartridge():
    image = Image.open(os.path.join(SOURCE, 'minidisc.png')).convert('RGB')
    width, height = image.size
    paper = image.convert('L').getpixel((4, 4))
    # Everything the backdrop reaches without crossing the black outline is outside the
    # cartridge: the backdrop itself, the soft shadow on it and the blurred rim of the
    # outline. What is darker than the backdrop there becomes black of that strength.
    marker = (255, 0, 255)
    filled = image.copy()
    for corner in ((0, 0), (width - 1, 0), (0, height - 1), (width - 1, height - 1)):
        ImageDraw.floodfill(filled, corner, marker, thresh=300)
    inside = ImageChops.difference(filled, Image.new('RGB', filled.size, marker)).convert('L').point(lambda v: 255 if v else 0)
    floor = paper - 5
    shade = image.convert('L').point(lambda v: max(0, min(255, int(round((floor - v) * 255.0 / floor)))))
    alpha = ImageChops.lighter(inside, shade)
    colour = Image.composite(image, Image.new('RGB', image.size, (0, 0, 0)), inside)
    colour.putalpha(alpha)
    shrink(square(colour, 0.0), 256).save(os.path.join(OUT, 'minidisc.png'), optimize=True)


def glyph():
    image = Image.open(os.path.join(SOURCE, 'minidisc-glyph.png')).convert('L')
    paper = max(image.getpixel((4, 4)), image.getpixel((image.size[0] - 5, image.size[1] - 5)))
    ink = image.getextrema()[0]
    alpha = image.point(lambda v: max(0, min(255, int(round((paper - 6 - v) * 255.0 / (paper - 6 - ink - 6))))))
    picture = Image.new('RGBA', image.size, (0, 0, 0, 0))
    picture.putalpha(alpha)
    shrink(square(picture, 0.0), 128).save(os.path.join(OUT, 'minidisc-glyph.png'), optimize=True)


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    cartridge()
    glyph()
    for name in ('minidisc.png', 'minidisc-glyph.png'):
        path = os.path.join(OUT, name)
        print('%s: %d bytes' % (path, os.path.getsize(path)))
