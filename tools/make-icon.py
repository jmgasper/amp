#!/usr/bin/env python3
"""Build Amp's application icon: a small amplifier with a Tasmanian-devil-black cabinet,
a warm speaker cone and a bright musical note, drawn to read from 16 px up to 128 px.

    python3 tools/make-icon.py resources/branding/amp-icon.hvif [preview.png]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hvif  # noqa: E402

C = hvif.hex_color


def curve(point, pin, pout):
    return (point, pin, pout)


def circle(cx, cy, r):
    k = 0.5523 * r
    return {'closed': True, 'points': [
        curve((cx + r, cy), (cx + r, cy - k), (cx + r, cy + k)),
        curve((cx, cy + r), (cx + k, cy + r), (cx - k, cy + r)),
        curve((cx - r, cy), (cx - r, cy + k), (cx - r, cy - k)),
        curve((cx, cy - r), (cx - k, cy - r), (cx + k, cy - r)),
    ]}


def rounded_rect(l, t, r, b, rad):
    k = 0.5523 * rad
    return {'closed': True, 'points': [
        curve((l + rad, t), (l + rad - k, t), (l + rad + k, t)),
        (r - rad, t),
        curve((r, t + rad), (r, t + rad - k), (r, t + rad + k)),
        (r, b - rad),
        curve((r - rad, b), (r - rad + k, b), (r - rad - k, b)),
        (l + rad, b),
        curve((l, b - rad), (l, b - rad + k), (l, b - rad - k)),
        (l, t + rad),
    ]}


# ------------------------------------------------------------------ styles
OUTLINE = {'color': C('1c1a1a')}
CABINET = hvif.linear_gradient((10, 8), (54, 58), [
    (0.0, C('4a4a4e')), (0.5, C('2b2b2f')), (1.0, C('141416'))])
CABINET_TOP = hvif.linear_gradient((10, 8), (54, 14), [(0.0, C('6c6c72')), (1.0, C('3a3a3f'))])
GRILLE = hvif.linear_gradient((14, 24), (50, 56), [(0.0, C('c9b48c')), (1.0, C('8d7551'))])
GRILLE_DARK = {'color': C('6b5738')}
CONE = hvif.radial_gradient((31, 40), 12, [(0.0, C('3a3a3d')), (0.7, C('222225')), (1.0, C('101012'))])
CONE_RING = {'color': C('5a5a60')}
DUST_CAP = hvif.radial_gradient((29, 38), 4, [(0.0, C('8a8a90')), (1.0, C('3c3c40'))])
KNOB = hvif.linear_gradient((0, 12), (0, 20), [(0.0, C('e8e8ec')), (1.0, C('9a9aa2'))])
KNOB_DOT = {'color': C('d62c2c')}
NOTE = hvif.linear_gradient((36, 4), (58, 30), [(0.0, C('ffd94a')), (1.0, C('f29a1a'))])
NOTE_OUTLINE = {'color': C('7a4a08')}
SHADOW = hvif.radial_gradient((32, 60), 24, [
    (0.0, (20, 20, 20, 110)), (0.7, (20, 20, 20, 40)), (1.0, (20, 20, 20, 0))], ratio=0.16)
HIGHLIGHT = {'color': (255, 255, 255, 60)}

style_names = ['outline', 'cabinet', 'cabinetTop', 'grille', 'grilleDark', 'cone', 'coneRing', 'dustCap', 'knob',
               'knobDot', 'note', 'noteOutline', 'shadow', 'highlight']
styles = [OUTLINE, CABINET, CABINET_TOP, GRILLE, GRILLE_DARK, CONE, CONE_RING, DUST_CAP, KNOB, KNOB_DOT, NOTE,
          NOTE_OUTLINE, SHADOW, HIGHLIGHT]
S = {name: index for index, name in enumerate(style_names)}

# ------------------------------------------------------------------ paths
cabinet = rounded_rect(8, 10, 56, 58, 5)
cabinet_top = rounded_rect(8, 10, 56, 21, 5)
top_line = {'closed': False, 'points': [(9, 21), (55, 21)]}
grille = rounded_rect(12, 24, 52, 55, 3)
grille_lines = {'closed': False, 'points': [(12, 31), (52, 31), (52, 33), (12, 33), (12, 40), (52, 40), (52, 42), (12, 42),
                                            (12, 49), (52, 49)]}
cone_outer = circle(31, 40, 12)
cone_ring = circle(31, 40, 9.5)
cone_inner = circle(31, 40, 7.5)
dust_cap = circle(30, 39, 3.2)
knob1 = circle(16, 15.5, 3.2)
knob2 = circle(25, 15.5, 3.2)
knob1_dot = circle(16, 13.3, 0.8)
knob2_dot = circle(25, 13.3, 0.8)
led = circle(48, 15.5, 1.6)
# eighth note over the top-right corner
note = {'closed': True, 'points': [
    curve((44, 27), (40, 27), (48, 27)),
    curve((49, 22), (49, 24.5), (49, 20)),
    (49, 8),
    curve((60, 12), (54, 7), (60, 9)),
    curve((57, 18), (60, 15), (56, 19)),
    (55, 12),
    (52, 11),
    (52, 22),
    curve((44, 31), (52, 27), (46.5, 31)),
    curve((39, 27), (40, 31), (39, 28)),
]}
note_small = {'closed': True, 'points': [
    curve((44, 27), (39, 27), (49, 27)),
    (50, 22),
    (50, 6),
    (61, 11),
    (61, 16),
    (54, 12),
    (54, 22),
    curve((44, 32), (54, 29), (46, 32)),
    curve((38, 27), (39, 32), (38, 28)),
]}
shadow = circle(32, 60, 1)
highlight = {'closed': False, 'points': [(11, 12), (13, 11), (52, 11)]}

path_names = ['cabinet', 'cabinetTop', 'topLine', 'grille', 'grilleLines', 'coneOuter', 'coneRing', 'coneInner',
              'dustCap', 'knob1', 'knob2', 'knob1Dot', 'knob2Dot', 'led', 'note', 'noteSmall', 'shadow', 'highlight']
paths = [cabinet, cabinet_top, top_line, grille, grille_lines, cone_outer, cone_ring, cone_inner, dust_cap, knob1, knob2,
         knob1_dot, knob2_dot, led, note, note_small, shadow, highlight]
P = {name: index for index, name in enumerate(path_names)}


def shape(style, *names, **extra):
    result = {'style': S[style], 'paths': [P[n] for n in names]}
    result.update(extra)
    return result


thin = {'type': 'stroke', 'width': 1, 'join': 2, 'cap': 2, 'miter': 4}
contour_large = {'type': 'contour', 'width': 3, 'join': 2, 'miter': 4}
contour_small = {'type': 'contour', 'width': 2, 'join': 2, 'miter': 4}
note_contour = {'type': 'contour', 'width': 2.5, 'join': 2, 'miter': 4}

SMALL = {'lod': (0.0, 0.5)}
LARGE = {'lod': (0.5, 4.0)}
DETAIL = {'lod': (0.95, 4.0)}

shapes = [
    shape('shadow', 'shadow', matrix=[24.0, 0.0, 0.0, 4.0, 32.0 - 32.0 * 24.0, 60.0 - 60.0 * 4.0]),
    shape('outline', 'cabinet', transformers=[contour_large], **LARGE),
    shape('outline', 'cabinet', transformers=[contour_small], **SMALL),
    shape('cabinet', 'cabinet'),
    shape('cabinetTop', 'cabinetTop'),
    shape('highlight', 'highlight', transformers=[thin], **DETAIL),
    shape('outline', 'topLine', transformers=[thin], **LARGE),
    shape('grille', 'grille'),
    shape('grilleDark', 'grilleLines', transformers=[thin], **DETAIL),
    shape('outline', 'coneOuter', **LARGE),
    shape('coneRing', 'coneRing', **LARGE),
    shape('cone', 'coneInner'),
    shape('cone', 'coneOuter', **SMALL),
    shape('dustCap', 'dustCap', **LARGE),
    shape('outline', 'knob1', 'knob2', transformers=[contour_small], **LARGE),
    shape('knob', 'knob1', 'knob2', **LARGE),
    shape('knobDot', 'knob1Dot', 'knob2Dot', **DETAIL),
    shape('knobDot', 'led', **LARGE),
    shape('noteOutline', 'note', transformers=[note_contour], **LARGE),
    shape('note', 'note', **LARGE),
    shape('noteOutline', 'noteSmall', transformers=[contour_small], **SMALL),
    shape('note', 'noteSmall', **SMALL),
]

icon = {'styles': styles, 'paths': paths, 'shapes': shapes}

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'amp-icon.hvif'
    data = hvif.encode(icon)
    hvif.decode(data)
    with open(out, 'wb') as f:
        f.write(data)
    print('%s: %d bytes' % (out, len(data)))
    svg = out.rsplit('.', 1)[0] + '.svg'
    with open(svg, 'w') as f:
        f.write(hvif.to_svg(icon))
    print('%s: SVG source' % svg)
    if len(sys.argv) > 2:
        hvif.preview(icon, 256, (245, 245, 240, 255)).save(sys.argv[2])
        print(sys.argv[2])
