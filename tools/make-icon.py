#!/usr/bin/env python3
"""Build Amp's application icon: a chrome loudspeaker, turned a little to the left, with a
large glossy blue eighth note in front of it. Haiku application icons are vector drawings
(HVIF), so the artwork in resources/branding/source/amp-icon.png is redrawn here: the
speaker from ellipses fitted to the edges of the picture and gradients sampled from it, the
note and the underside of its flag traced from it. Fine detail (screws, highlights, the
note's shadow) is left out at small sizes, where it would only blur.

    python3 tools/make-icon.py resources/branding/amp-icon.hvif [preview.png]
    python3 tools/make-icon.py --import new-artwork.png

--import turns a picture on a white background into the source artwork: the white becomes
transparent and the grey shadow under the speaker a black one of the same depth.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hvif  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARTWORK = os.path.join(ROOT, 'resources', 'branding', 'source', 'amp-icon.png')

# The artwork is 1254 pixels square. The speaker and the note span x 108..1190; on the
# 64 x 64 canvas they get 60 units, centred, with the floor shadow below.
SCALE = 60.0 / 1082.0
CENTER = (649.0, 605.0)
ORIGIN = (32.0, 32.0)


def canvas(x, y):
    return ((x - CENTER[0]) * SCALE + ORIGIN[0], (y - CENTER[1]) * SCALE + ORIGIN[1])


def rgb(r, g, b, a=255):
    return (r, g, b, a)


# ------------------------------------------------------------------ the artwork's geometry
# Ellipses as (centre x, centre y, radius x, radius y, rotation in degrees), in artwork
# pixels, least-squares fits to edges found along rays from the centre of the speaker.
BACK = (507.4, 625.4, 399.0, 406.2, 13.66)     # the back of the body, seen past the ring
RIM = (601.0, 603.5, 411.3, 449.8, -18.21)     # the front ring with its bevel
FACE = (610.0, 603.5, 402.3, 449.8, -18.21)    # the chrome face of the ring (bright lip)
CHROME = (611.5, 604.5, 393.0, 441.0, -18.21)  # the brushed face inside the lip
WELL = (617.0, 598.0, 322.65, 359.66, -14.76)  # the step down from the ring
SURROUND = (613.65, 603.31, 308.66, 352.0, -12.41)  # the rubber roll around the cone
CONE = (609.72, 604.63, 258.77, 301.14, -14.15)
CAP = (571.47, 610.74, 106.37, 117.9, -4.79)   # the dust cap
SCREWS = [(334.0, 335.0, 29.0), (362.0, 877.0, 30.0), (823.0, 962.0, 27.0)]


def point_on(e, t, grow=0.0):
    cx, cy, rx, ry, angle = e
    a = math.radians(angle)
    u, v = (rx + grow) * math.cos(t), (ry + grow) * math.sin(t)
    return (cx + u * math.cos(a) - v * math.sin(a), cy + u * math.sin(a) + v * math.cos(a))


def tangent_at(e, t, grow=0.0):
    cx, cy, rx, ry, angle = e
    a = math.radians(angle)
    u, v = -(rx + grow) * math.sin(t), (ry + grow) * math.cos(t)
    return (u * math.cos(a) - v * math.sin(a), u * math.sin(a) + v * math.cos(a))


def arc(e, t0, t1, grow=0.0, first_in=True, last_out=True):
    """Path points for the arc of ellipse `e` from t0 to t1 (radians, t1 > t0) in cubic
    Beziers of at most a quarter turn each. The handles leaving the ends are dropped when
    `first_in`/`last_out` are false, for a straight line to meet them."""
    pieces = max(1, int(math.ceil((t1 - t0) / (math.pi / 2) - 1e-9)))
    step = (t1 - t0) / pieces
    h = 4.0 / 3.0 * math.tan(step / 4.0)
    points = []
    for i in range(pieces + 1):
        t = t0 + i * step
        x, y = point_on(e, t, grow)
        dx, dy = tangent_at(e, t, grow)
        here = canvas(x, y)
        before = canvas(x - h * dx, y - h * dy) if (i > 0 or first_in) else here
        after = canvas(x + h * dx, y + h * dy) if (i < pieces or last_out) else here
        points.append((here, before, after))
    return points


def ellipse(e, grow=0.0, shift=(0.0, 0.0)):
    moved = (e[0] + shift[0], e[1] + shift[1]) + tuple(e[2:])
    return {'closed': True, 'points': arc(moved, 0.0, 2 * math.pi, grow)[:-1]}


def circle(x, y, r):
    return ellipse((x, y, r, r, 0.0))


def hull(*ellipses):
    """The outline around several ellipses (the side of a cylinder around both of its ends):
    arcs of the ellipses joined by the tangents between them."""
    steps = 720
    samples = []
    for index, e in enumerate(ellipses):
        for k in range(steps):
            x, y = point_on(e, 2 * math.pi * k / steps)
            samples.append((x, y, index, k))
    samples.sort()

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower, upper = [], []
    for p in samples:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(samples):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    ring = lower[:-1] + upper[:-1]  # the direction in which t grows
    # start at the beginning of a run on one ellipse
    first = next(i for i in range(len(ring)) if ring[i][2] != ring[i - 1][2])
    ring = ring[first:] + ring[:first]
    runs = []
    for p in ring:
        if runs and runs[-1][0] == p[2]:
            runs[-1][2] = p[3]
        else:
            runs.append([p[2], p[3], p[3]])
    points = []
    for index, k0, k1 in runs:
        t0 = 2 * math.pi * k0 / steps
        t1 = t0 + 2 * math.pi * ((k1 - k0) % steps) / steps
        points += arc(ellipses[index], t0, t1, first_in=False, last_out=False)
    return {'closed': True, 'points': points}


# ------------------------------------------------------------------ tracing

def load_artwork():
    from PIL import Image
    return Image.open(ARTWORK).convert('RGBA')


def note_mask(image):
    """The note: the only strongly blue part of the picture, its highlights filled in."""
    from PIL import Image, ImageDraw
    width, height = image.size
    source = image.load()
    mask = Image.new('L', image.size, 0)
    pixels = mask.load()
    for y in range(height):
        for x in range(width):
            r, g, b, a = source[x, y]
            if a > 128 and b - r > 90:
                pixels[x, y] = 255
    if mask.getpixel((800, 800)) != 255:
        raise SystemExit('the artwork changed: no note at (800, 800)')
    ImageDraw.floodfill(mask, (0, 0), 128)
    return mask.point(lambda v: 0 if v == 128 else 255)


def erode(mask, pixels):
    from PIL import ImageFilter
    for _ in range(pixels):
        mask = mask.filter(ImageFilter.MinFilter(3))
    return mask


def dilate(mask, pixels):
    from PIL import ImageFilter
    for _ in range(pixels):
        mask = mask.filter(ImageFilter.MaxFilter(3))
    return mask


def smooth(mask, radius=2.0):
    from PIL import ImageFilter
    return mask.filter(ImageFilter.GaussianBlur(radius)).point(lambda v: 255 if v >= 128 else 0)


def flag_underside(image, note):
    """The dark underside of the flag, below the ridge that runs from the stem to its tip."""
    from PIL import Image, ImageChops, ImageDraw
    width, height = image.size
    source = image.load()
    dark = Image.new('L', image.size, 0)
    pixels = dark.load()
    for y in range(300, 620):
        for x in range(946, width):
            r, g, b, a = source[x, y]
            if a > 128 and r < 12 and g < 112:
                pixels[x, y] = 255
    dark = smooth(ImageChops.darker(dark, erode(note, 8)), 3.0)
    seed = (1060, 430)
    if dark.getpixel(seed) != 255:
        raise SystemExit('the artwork changed: no flag underside at %r' % (seed,))
    ImageDraw.floodfill(dark, seed, 128)
    band = dark.point(lambda v: 255 if v == 128 else 0)
    # where the band meets the edge of the flag it runs on into the dark edge of the note
    edge = ImageChops.subtract(note, erode(note, 8))
    edge.paste(0, (0, 0, 944, height))  # into the crease at the stem, not down the stem
    edge.paste(0, (0, 430, 985, height))
    return smooth(ImageChops.lighter(band, ImageChops.darker(dilate(band, 10), edge)), 1.5)


def ellipse_mask(size, e):
    from PIL import Image, ImageDraw
    mask = Image.new('L', size, 0)
    ImageDraw.Draw(mask).polygon([point_on(e, 2 * math.pi * k / 360) for k in range(360)], fill=255)
    return mask


def cast_shadow(note, dx, dy):
    """The note's shadow on the speaker when it falls `dx`, `dy` pixels away: the note moved
    and cut to the ring, so no shadow lies on the background."""
    from PIL import Image, ImageChops
    moved = Image.new('L', note.size, 0)
    moved.paste(note, (dx, dy))
    return ImageChops.darker(moved, ellipse_mask(note.size, RIM))


def trace(mask, tolerance):
    """The outline of the white area of `mask` as canvas points (the part that holds the
    top-most white pixel)."""
    width, height = mask.size
    pixels = mask.load()

    def filled(x, y):
        return 0 <= x < width and 0 <= y < height and pixels[x, y] == 255

    left, top, right, bottom = mask.getbbox()
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
        if len(outline) > 16 * (width + height):
            raise SystemExit('an outline does not close')
    points = simplify_closed(outline, tolerance)
    while len(points) > 200:
        tolerance *= 1.3
        points = simplify_closed(outline, tolerance)
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


def import_artwork(path):
    """A picture on white -> the source artwork: white is transparent, the grey floor shadow
    becomes black of the same depth, and the pixels along the outline keep only as much of
    the object as they hold, so no white fringe is left."""
    from collections import deque
    from PIL import Image
    image = Image.open(path).convert('RGB')
    width, height = image.size
    source = image.load()
    lum = [[0.0] * width for _ in range(height)]
    grey = [[False] * width for _ in range(height)]
    for y in range(height):
        for x in range(width):
            r, g, b = source[x, y]
            lum[y][x] = (r + g + b) / 3.0
            grey[y][x] = max(r, g, b) - min(r, g, b) <= 6
    # The backdrop and the shadow on it are grey and change slowly; the objects are tinted
    # or meet the backdrop at a sharp edge.
    outside = [[False] * width for _ in range(height)]
    queue = deque()
    for x in range(width):
        for y in (0, height - 1):
            outside[y][x] = True
            queue.append((x, y))
    for y in range(height):
        for x in (0, width - 1):
            outside[y][x] = True
            queue.append((x, y))
    while queue:
        x, y = queue.popleft()
        here = lum[y][x]
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < width and 0 <= ny < height and not outside[ny][nx] \
                    and grey[ny][nx] and abs(lum[ny][nx] - here) <= 9:
                outside[ny][nx] = True
                queue.append((nx, ny))
    # specks of colour in the shadow are noise, not objects
    seen = [[False] * width for _ in range(height)]
    for y in range(height):
        for x in range(width):
            if outside[y][x] or seen[y][x]:
                continue
            seen[y][x] = True
            part, queue = [(x, y)], deque([(x, y)])
            while queue:
                px, py = queue.popleft()
                for nx, ny in ((px + 1, py), (px - 1, py), (px, py + 1), (px, py - 1)):
                    if 0 <= nx < width and 0 <= ny < height and not outside[ny][nx] and not seen[ny][nx]:
                        seen[ny][nx] = True
                        part.append((nx, ny))
                        queue.append((nx, ny))
            if len(part) < 500:
                for px, py in part:
                    outside[py][px] = True
    paper = 251.0
    result = Image.new('RGBA', image.size)
    out = result.load()
    for y in range(height):
        for x in range(width):
            if outside[y][x]:
                out[x, y] = (0, 0, 0, int(round(255 * max(0.0, paper - lum[y][x]) / paper)))
            else:
                r, g, b = source[x, y]
                out[x, y] = (r, g, b, 255)
    # The outline: a pixel of the object within two pixels of the backdrop mixes the
    # object's colour O with the backdrop B (white or shadow). Unmix it into O at the
    # coverage it had, O taken from the solid object a little further in.
    far = 3
    distance = [[0 if outside[y][x] else far for x in range(width)] for y in range(height)]
    queue = deque((x, y) for y in range(height) for x in range(width) if outside[y][x])
    while queue:
        x, y = queue.popleft()
        for nx in (x - 1, x, x + 1):
            for ny in (y - 1, y, y + 1):
                if 0 <= nx < width and 0 <= ny < height and distance[ny][nx] > distance[y][x] + 1:
                    distance[ny][nx] = distance[y][x] + 1
                    queue.append((nx, ny))
    for y in range(height):
        for x in range(width):
            if distance[y][x] not in (1, 2):
                continue
            window = [(x + dx, y + dy) for dx in range(-4, 5) for dy in range(-4, 5)
                      if 0 <= x + dx < width and 0 <= y + dy < height]
            back = [p for p in window if distance[p[1]][p[0]] == 0 and abs(p[0] - x) <= 2 and abs(p[1] - y) <= 2]
            inner = [p for p in window if distance[p[1]][p[0]] == far]
            if not inner:  # a sliver: it is as much backdrop as object
                out[x, y] = (0, 0, 0, int(round(255 * max(0.0, paper - lum[y][x]) / paper)))
                continue
            if not back:
                continue
            nearest = min(abs(p[0] - x) + abs(p[1] - y) for p in inner)
            inner = [p for p in inner if abs(p[0] - x) + abs(p[1] - y) <= nearest + 1]
            obj = [sum(source[p][c] for p in inner) / len(inner) for c in range(3)]
            b = sum(lum[p[1]][p[0]] for p in back) / len(back)
            mix = source[x, y]
            d = [obj[c] - b for c in range(3)]
            norm = sum(v * v for v in d)
            coverage = 1.0 if norm < 1 else max(0.0, min(1.0, sum((mix[c] - b) * d[c] for c in range(3)) / norm))
            shade = max(0.0, paper - b) / paper
            alpha = coverage + (1 - coverage) * shade
            if alpha <= 0:
                out[x, y] = (0, 0, 0, 0)
                continue
            colour = tuple(int(round(min(255.0, coverage * obj[c] / alpha))) for c in range(3))
            out[x, y] = colour + (int(round(255 * alpha)),)
    return result


# ------------------------------------------------------------------ styles

def linear(a, b, stops):
    return hvif.linear_gradient(canvas(*a), canvas(*b), stops)


def radial(center, radius, stops, ratio=1.0, angle=0.0):
    """A circular gradient of `radius` artwork pixels, squeezed to `ratio` and turned."""
    x, y = canvas(*center)
    s = radius * SCALE / 64.0
    a = math.radians(angle)
    return {'gradient': hvif.GRADIENT_CIRCULAR,
            'matrix': [s * math.cos(a), s * math.sin(a), -s * ratio * math.sin(a), s * ratio * math.cos(a), x, y],
            'stops': [(o, tuple(c)) for o, c in stops]}


def on_ellipse(e, stops, grow=0.0, shift=(0.0, 0.0)):
    """A circular gradient laid over ellipse `e`: offset 1.0 is its outline."""
    cx, cy, rx, ry, angle = e
    x, y = canvas(cx + shift[0], cy + shift[1])
    a = math.radians(angle)
    sx, sy = (rx + grow) * SCALE / 64.0, (ry + grow) * SCALE / 64.0
    return {'gradient': hvif.GRADIENT_CIRCULAR,
            'matrix': [sx * math.cos(a), sx * math.sin(a), -sy * math.sin(a), sy * math.cos(a), x, y],
            'stops': [(o, tuple(c)) for o, c in stops]}


def build():
    image = load_artwork()
    note = note_mask(image)
    lit = smooth(erode(note, 5), 2.0)
    underside = flag_underside(image, note)

    styles, paths, shapes = [], [], []

    def style(s):
        styles.append(s)
        return len(styles) - 1

    def path(p):
        paths.append(p)
        return len(paths) - 1

    def shape(s, *ps, lod=None, matrix=None):
        entry = {'style': s, 'paths': list(ps)}
        if lod:
            entry['lod'] = lod
        if matrix:
            entry['matrix'] = matrix
        shapes.append(entry)

    LARGE = (0.9, 4.0)   # 64 pixels and more
    MEDIUM = (0.4, 4.0)  # 32 pixels and more

    # the floor shadow, deepest below the body
    floor = (540.0, 1060.0, 430.0, 56.0, 0.0)
    shape(style(on_ellipse(floor, [(0.0, rgb(0, 0, 0, 140)), (0.5, rgb(0, 0, 0, 80)), (1.0, rgb(0, 0, 0, 0))])),
          path(ellipse(floor)), lod=(0.3, 4.0))
    # the body behind the ring, lit from the upper left
    shape(style(linear((290, 290), (180, 790), [
        (0.0, rgb(126, 151, 189)), (0.3, rgb(112, 136, 172)), (0.62, rgb(80, 97, 120)), (1.0, rgb(46, 58, 72))])),
        path(hull(BACK, RIM)))
    # the ring: its side below, the bevel, the bright lip and the brushed face
    side = (RIM[0] - 4, RIM[1] + 17) + RIM[2:]
    shape(style(linear((300, 300), (650, 1080), [(0.0, rgb(150, 154, 160)), (1.0, rgb(78, 86, 98))])), path(ellipse(side)))
    shape(style({'color': rgb(152, 156, 161)}), path(ellipse(RIM)))
    shape(style(linear((330, 230), (840, 1010), [(0.0, rgb(252, 252, 252)), (0.5, rgb(208, 210, 213)), (1.0, rgb(162, 165, 170))])),
          path(ellipse(FACE)))
    shape(style(linear((300, 250), (850, 1000), [(0.0, rgb(230, 231, 233)), (0.5, rgb(197, 199, 203)), (1.0, rgb(150, 153, 159))])),
          path(ellipse(CHROME)))
    # the step down, the rubber roll with its sheen, the cone
    shape(style(linear((400, 300), (800, 900), [(0.0, rgb(112, 117, 125)), (1.0, rgb(88, 93, 102))])), path(ellipse(WELL)))
    shape(style(on_ellipse(SURROUND, [
        (0.0, rgb(20, 23, 28)), (0.83, rgb(20, 23, 28)), (0.845, rgb(16, 18, 22)), (0.885, rgb(50, 55, 64)),
        (0.915, rgb(80, 86, 98)), (0.95, rgb(50, 55, 64)), (0.985, rgb(22, 25, 30)), (1.0, rgb(12, 14, 18))])),
        path(ellipse(SURROUND)))
    shape(style(linear((430, 400), (700, 860), [
        (0.0, rgb(34, 38, 45)), (0.2, rgb(38, 42, 50)), (0.45, rgb(72, 78, 90)), (0.7, rgb(94, 101, 115)), (1.0, rgb(125, 132, 146))])),
        path(ellipse(CONE)))
    # the dust cap: its shadow on the cone, then a dark glossy dome lit from the upper left
    shape(style(on_ellipse(CAP, [(0.8, rgb(0, 0, 0, 150)), (0.9, rgb(0, 0, 0, 70)), (1.0, rgb(0, 0, 0, 0))], grow=26, shift=(4, 8))),
          path(ellipse(CAP, grow=26, shift=(4, 8))), lod=MEDIUM)
    shape(style(radial((528, 548), 200, [
        (0.0, rgb(228, 231, 236)), (0.1, rgb(206, 211, 218)), (0.36, rgb(112, 119, 132)), (0.6, rgb(58, 65, 77)),
        (0.86, rgb(38, 44, 55)), (0.93, rgb(46, 53, 66)), (1.0, rgb(84, 94, 112))])),
        path(ellipse(CAP)))
    # the screws on the ring: a countersink, shadowed at the upper left, and a domed head
    for x, y, r in SCREWS:
        shape(style(linear((x - r, y - r), (x + r, y + r), [(0.0, rgb(112, 116, 122)), (0.5, rgb(170, 173, 178)), (1.0, rgb(238, 239, 241))])),
              path(circle(x, y, r + 1)), lod=LARGE)
        shape(style({'color': rgb(48, 52, 58)}), path(circle(x + 1, y + 1, r - 4)), lod=LARGE)
        shape(style(radial((x - 8, y - 10), r * 1.5, [
            (0.0, rgb(226, 229, 233)), (0.2, rgb(160, 165, 172)), (0.55, rgb(92, 97, 105)), (1.0, rgb(60, 64, 71))])),
            path(circle(x, y, r - 6)), lod=LARGE)

    # the note: its shadow on the speaker, the dark blue edge, the lit face, the underside of
    # the flag and the gloss
    # three steps of shadow make it soft
    for dx, dy, alpha in ((-26, 16, 22), (-15, 10, 30), (-7, 5, 40)):
        shape(style({'color': rgb(0, 12, 34, alpha)}), path({'closed': True, 'points': trace(cast_shadow(note, dx, dy), 2.0)}),
              lod=MEDIUM)
    outline = path({'closed': True, 'points': trace(note, 1.2)})
    shape(style(linear((870, 110), (820, 945), [(0.0, rgb(16, 120, 220)), (1.0, rgb(0, 80, 178))])), outline)
    face = path({'closed': True, 'points': trace(lit, 1.2)})
    shape(style(linear((820, 130), (1080, 900), [
        (0.0, rgb(112, 210, 254)), (0.12, rgb(90, 199, 253)), (0.22, rgb(74, 189, 253)), (0.34, rgb(50, 168, 248)),
        (0.47, rgb(45, 165, 248)), (0.62, rgb(38, 152, 240)), (0.75, rgb(31, 143, 235)), (0.9, rgb(6, 118, 224)),
        (1.0, rgb(0, 104, 214))])), face)
    shape(style(linear((1085, 385), (1030, 475), [(0.0, rgb(2, 110, 214)), (1.0, rgb(0, 80, 176))])),
          path({'closed': True, 'points': trace(underside, 1.2)}))
    shape(style(radial((736, 704), 132, [
        (0.0, rgb(242, 252, 255, 250)), (0.25, rgb(206, 244, 254, 215)), (0.55, rgb(150, 226, 253, 110)), (1.0, rgb(120, 214, 252, 0))],
        ratio=0.6, angle=-28)), face)
    shape(style(radial((868, 138), 34, [(0.0, rgb(240, 252, 255, 230)), (0.5, rgb(200, 240, 255, 110)), (1.0, rgb(160, 225, 255, 0))],
                       ratio=0.7, angle=-60)), face, lod=LARGE)
    return {'styles': styles, 'paths': paths, 'shapes': shapes}


def snap(icon):
    """Coordinates on the HVIF grid (1/102 of a unit), so a handle that lands on its point
    is written as a straight segment and the file reads back exactly as it was written."""
    def grid(v):
        return v if float(v).is_integer() and -32 <= v <= 95 else round((v + 128.0) * 102.0) / 102.0 - 128.0

    for path in icon['paths']:
        points = [hvif.normalize_point(p) for p in path['points']]
        path['points'] = [tuple((grid(x), grid(y)) for x, y in p) for p in points]
    return icon


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--import':
        if len(sys.argv) != 3:
            raise SystemExit(__doc__)
        import_artwork(sys.argv[2]).save(ARTWORK)
        print(ARTWORK)
        sys.exit(0)
    out = sys.argv[1] if len(sys.argv) > 1 else 'amp-icon.hvif'
    icon = snap(build())
    data = hvif.encode(icon)
    hvif.decode(data)
    with open(out, 'wb') as f:
        f.write(data)
    print('%s: %d bytes, %d shapes, %d path points' % (out, len(data), len(icon['shapes']),
                                                       sum(len(p['points']) for p in icon['paths'])))
    svg = out.rsplit('.', 1)[0] + '.svg'
    with open(svg, 'w') as f:
        f.write(hvif.to_svg(icon))
    print('%s: SVG source' % svg)
    if len(sys.argv) > 2:
        hvif.preview(icon, 256, (245, 245, 240, 255)).save(sys.argv[2])
        print(sys.argv[2])
