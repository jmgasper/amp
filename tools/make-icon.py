#!/usr/bin/env python3
"""Build Amp's application icon: a green glass button with a white pair of beamed notes in a
brushed metal ring. Haiku application icons are vector drawings (HVIF), so the artwork in
resources/branding/source/amp-icon.png is redrawn here: the ring, the button and its gloss
from circles and gradients measured on the picture, the notes traced from it.

    python3 tools/make-icon.py resources/branding/amp-icon.hvif [preview.png]
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hvif  # noqa: E402

C = hvif.hex_color
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARTWORK = os.path.join(ROOT, 'resources', 'branding', 'source', 'amp-icon.png')

# The ring of the artwork is centred on (624, 625) and 492 pixels in radius; on the 64 x 64
# canvas it gets a radius of 30, which leaves room for the shadow below it.
CENTER = (624.0, 625.0)
SCALE = 30.0 / 492.0
ORIGIN = (32.0, 30.5)


def canvas(x, y):
    return ((x - CENTER[0]) * SCALE + ORIGIN[0], (y - CENTER[1]) * SCALE + ORIGIN[1])


def curve(point, pin, pout):
    return (point, pin, pout)


def circle(cx, cy, r, ry=None):
    ry = r if ry is None else ry
    k, ky = 0.5523 * r, 0.5523 * ry
    return {'closed': True, 'points': [
        curve((cx + r, cy), (cx + r, cy - ky), (cx + r, cy + ky)),
        curve((cx, cy + ry), (cx + k, cy + ry), (cx - k, cy + ry)),
        curve((cx - r, cy), (cx - r, cy + ky), (cx - r, cy - ky)),
        curve((cx, cy - ry), (cx - k, cy - ry), (cx + k, cy - ry)),
    ]}


def artwork_circle(x, y, r):
    cx, cy = canvas(x, y)
    return circle(cx, cy, r * SCALE)


# ------------------------------------------------------------------ tracing

def trace_notes():
    """The outline of the notes as canvas points, read from the artwork."""
    from PIL import Image, ImageChops, ImageDraw
    image = Image.open(ARTWORK).convert('RGB')
    width, height = image.size
    # the notes are the only white inside the button; their lower halves are shaded, but
    # still far from the green around them, which has next to no blue
    red, green, blue = image.split()
    white = ImageChops.darker(red.point(lambda v: 255 if v >= 150 else 0), blue.point(lambda v: 255 if v >= 150 else 0))
    seed = (494, 789)  # inside the left note head
    if white.getpixel(seed) != 255:
        raise SystemExit('the artwork changed: no note at %r' % (seed,))
    ImageDraw.floodfill(white, seed, 128)
    inside = white.point(lambda v: 1 if v == 128 else 0)
    pixels = inside.load()

    def filled(x, y):
        return 0 <= x < width and 0 <= y < height and pixels[x, y] == 1

    left, top, right, bottom = inside.point(lambda v: 255 if v else 0).getbbox()
    start = next((x, top) for x in range(left, right) if filled(x, top))
    # walk the cracks between pixels with the shape on the right-hand side
    heading = {(1, 0): ((0, 0), (0, -1)), (0, 1): ((-1, 0), (0, 0)), (-1, 0): ((-1, -1), (-1, 0)), (0, -1): ((0, -1), (-1, -1))}
    left_of = {(1, 0): (0, -1), (0, -1): (-1, 0), (-1, 0): (0, 1), (0, 1): (1, 0)}
    right_of = {v: k for k, v in left_of.items()}
    at, direction = start, (1, 0)
    outline = []
    while True:
        outline.append(at)
        at = (at[0] + direction[0], at[1] + direction[1])
        (rx, ry), (lx, ly) = heading[direction]
        ahead_right = filled(at[0] + rx, at[1] + ry)
        ahead_left = filled(at[0] + lx, at[1] + ly)
        if ahead_left:
            direction = left_of[direction]
        elif not ahead_right:
            direction = right_of[direction]
        if at == start and direction == (1, 0):
            break
        if len(outline) > 4 * (width + height) * 4:
            raise SystemExit('the outline of the notes does not close')
    points = simplify_closed(outline, 0.9)
    if len(points) > 250:
        points = simplify_closed(outline, 1.6)
    return [canvas(x, y) for x, y in points]


def simplify(points, tolerance):
    """Ramer-Douglas-Peucker without recursion."""
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        first, last = stack.pop()
        (ax, ay), (bx, by) = points[first], points[last]
        length = math.hypot(bx - ax, by - ay)
        worst, index = 0.0, -1
        for i in range(first + 1, last):
            px, py = points[i]
            if length == 0:
                distance = math.hypot(px - ax, py - ay)
            else:
                distance = abs((bx - ax) * (ay - py) - (ax - px) * (by - ay)) / length
            if distance > worst:
                worst, index = distance, i
        if worst > tolerance:
            keep[index] = True
            stack.append((first, index))
            stack.append((index, last))
    return [p for p, k in zip(points, keep) if k]


def simplify_closed(points, tolerance):
    far = max(range(len(points)), key=lambda i: math.hypot(points[i][0] - points[0][0], points[i][1] - points[0][1]))
    first = simplify(points[:far + 1], tolerance)
    second = simplify(points[far:] + [points[0]], tolerance)
    return first[:-1] + second[:-1]


def gloss_outline():
    """The highlight on the glass: the part of the button inside a circle laid over its upper left."""
    bx, by = canvas(624, 609)
    br = 338 * SCALE
    gx, gy = canvas(554, 249)
    gr = 372 * SCALE
    points = []
    steps = 96
    for i in range(steps):
        angle = 2 * math.pi * i / steps
        x, y = bx + br * math.cos(angle), by + br * math.sin(angle)
        if math.hypot(x - gx, y - gy) <= gr:
            points.append((angle, x, y, 0))
    arc = []
    for i in range(steps):
        angle = 2 * math.pi * i / steps
        x, y = gx + gr * math.cos(angle), gy + gr * math.sin(angle)
        if math.hypot(x - bx, y - by) <= br:
            arc.append((angle, x, y))
    # the rim of the button runs clockwise from the left to the upper right; the edge of the
    # gloss closes the shape from there back to the left
    rim = sorted(points, key=lambda p: (p[0] - math.pi / 2) % (2 * math.pi))
    arc = sorted(arc, key=lambda p: p[0])
    return [(x, y) for _, x, y, _ in rim] + [(x, y) for _, x, y in arc]


# ------------------------------------------------------------------ styles
SHADOW_AT = canvas(624, 1092)
SHADOW = hvif.radial_gradient(SHADOW_AT, 25.5, [
    (0.0, (120, 120, 120, 150)), (0.5, (130, 130, 130, 75)), (1.0, (140, 140, 140, 0))], ratio=0.26)
RIM = hvif.linear_gradient((32, 0.5), (32, 60.5), [(0.0, C('b4b4b4')), (0.5, C('7c7d7f')), (1.0, C('26282a'))])
# brushed metal: brightest at the upper left, a second, weaker sheen opposite
METAL = {'gradient': hvif.GRADIENT_CONIC, 'stops': [
    (0.0, C('ebebea')), (0.25, C('d2d3d4')), (0.5, C('a3a6aa')), (0.78, C('a0a3a8')), (1.0, C('c2c3c5'))]}
ANGLE = math.radians(-135)  # gradient x axis towards the upper left
METAL['matrix'] = [math.cos(ANGLE), math.sin(ANGLE), -math.sin(ANGLE), math.cos(ANGLE), ORIGIN[0], ORIGIN[1]]
METAL_EDGE = hvif.linear_gradient((32, 1.5), (32, 59.5), [(0.0, (255, 255, 255, 150)), (0.4, (255, 255, 255, 0)), (1.0, (255, 255, 255, 0))])
WALL = hvif.linear_gradient((32, 6.5), (32, 53), [(0.0, C('3c3c3e')), (0.45, C('6e6f71')), (0.8, C('d8d9da')), (1.0, C('f4f4f4'))])
GAP = {'color': C('03190a')}
GREEN = hvif.linear_gradient(canvas(470, 260), canvas(780, 960), [
    (0.0, C('7fcd3c')), (0.3, C('4aa526')), (0.65, C('247d1d')), (1.0, C('17651a'))])
GLOW = hvif.radial_gradient(canvas(624, 1040), 420 * SCALE, [
    (0.0, (120, 210, 130, 170)), (0.45, (90, 180, 105, 80)), (1.0, (70, 160, 90, 0))], ratio=0.55)
GLOSS = hvif.linear_gradient(canvas(480, 250), canvas(560, 620), [
    (0.0, (255, 255, 255, 105)), (0.6, (255, 255, 255, 40)), (1.0, (255, 255, 255, 12))])
NOTE_SHADOW = {'color': (4, 50, 12, 55)}
NOTE = hvif.linear_gradient(canvas(624, 440), canvas(624, 850), [(0.0, C('fbfdf8')), (1.0, C('dcebd6'))])

style_names = ['shadow', 'rim', 'metal', 'metalEdge', 'wall', 'gap', 'green', 'glow', 'gloss', 'noteShadow', 'note']
styles = [SHADOW, RIM, METAL, METAL_EDGE, WALL, GAP, GREEN, GLOW, GLOSS, NOTE_SHADOW, NOTE]
S = {name: index for index, name in enumerate(style_names)}

# ------------------------------------------------------------------ paths
# gradients follow the matrix of a shape, so the ellipse is drawn as one
shadow = circle(SHADOW_AT[0], SHADOW_AT[1], 25.5, 25.5 * 0.26)
rim = artwork_circle(624, 625, 492)
metal = artwork_circle(624, 625, 477)
wall = artwork_circle(624, 613, 381)
gap = artwork_circle(624, 609, 367)
button = artwork_circle(624, 609, 356)
notes = {'closed': True, 'points': trace_notes()}
gloss = {'closed': True, 'points': gloss_outline()}

path_names = ['shadow', 'rim', 'metal', 'wall', 'gap', 'button', 'notes', 'gloss']
paths = [shadow, rim, metal, wall, gap, button, notes, gloss]
P = {name: index for index, name in enumerate(path_names)}


def shape(style, *names, **extra):
    result = {'style': S[style], 'paths': [P[n] for n in names]}
    result.update(extra)
    return result


LARGE = {'lod': (0.4, 4.0)}

shapes = [
    shape('shadow', 'shadow'),
    shape('rim', 'rim'),
    shape('metal', 'metal'),
    shape('metalEdge', 'metal', **LARGE),
    shape('wall', 'wall'),
    shape('gap', 'gap'),
    shape('green', 'button'),
    shape('glow', 'button'),
    shape('gloss', 'gloss', **LARGE),
    shape('noteShadow', 'notes', matrix=[1.0, 0.0, 0.0, 1.0, 0.6, 1.0], **LARGE),
    shape('noteShadow', 'notes', matrix=[1.0, 0.0, 0.0, 1.0, 0.3, 0.5]),
    shape('note', 'notes'),
]

icon = {'styles': styles, 'paths': paths, 'shapes': shapes}

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'amp-icon.hvif'
    data = hvif.encode(icon)
    hvif.decode(data)
    with open(out, 'wb') as f:
        f.write(data)
    print('%s: %d bytes, the notes have %d points' % (out, len(data), len(notes['points'])))
    svg = out.rsplit('.', 1)[0] + '.svg'
    with open(svg, 'w') as f:
        f.write(hvif.to_svg(icon))
    print('%s: SVG source' % svg)
    if len(sys.argv) > 2:
        hvif.preview(icon, 256, (245, 245, 240, 255)).save(sys.argv[2])
        print(sys.argv[2])
