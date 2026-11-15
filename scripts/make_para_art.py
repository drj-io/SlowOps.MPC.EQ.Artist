#!/usr/bin/env python3
"""Draw the curve and PARA images into plugins/<EQA_VARIANT>/skins/ (needs Pillow). Run after
make_skin_art.py and make_text_art.py (bg_para.png starts as their graph with axis labels):

    EQA_VARIANT=lines python3 scripts/make_skin_art.py && EQA_VARIANT=lines $PILLOW_PY scripts/make_text_art.py \\
        && EQA_VARIANT=lines $PILLOW_PY scripts/make_para_art.py        (scripts/make_art.sh runs all three for both)

Curves, by plugin:
  lines  para_line_t0..2.png    a curve in white (LineStrip76: 76 columns of 16 px, 0.25 dB vertices, 25 rises,
                                3,625 frames), each column cut into 3 tiles of 144 px. A segment lying on
                                0 dB is faint, so a band's curve stands out only where it has shape.
         para_bgline_t0..2.png  the same in grey (PARA's combined curve, behind)
  dots   para_dot_upper/lower   white dots (ShadeStrip: 64 columns of 19 px, 0.5 dB), upper and lower halves
         para_bgdot_upper/lower grey dots
  both   para_fill.png          the selected band's flat fill, one strip for all band colours: frame =
                                colour x height (145 heights, 0.25 dB); as wide as this plugin's columns
Both: para_node_1..8.png (numbered band nodes), para_ring_1..8.png (selection ring), para_pen.png (blank),
pband_* (band buttons, NONE), pminus / pplus, pseg_<type> (TYPE switch); captions on bg_para.png;
the version stamp ("<plugin name> v<version>" from plugin.json) on bg.png and bg_para.png.
"""
import json
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skin_layout as L  # noqa: E402
import make_text_art as T  # noqa: E402  (face(), the button drawing)

OUT = L.SKINS
FONT = os.path.join(L.ROOT, 'assets', 'fonts', 'TitilliumWeb-Bold.ttf')
SS = 3


def font(px):
    return ImageFont.truetype(FONT, px * SS)


def line_frame(w, h, left_db, right_db, colour, alpha, width):
    """One column of a curve: the segment from the left vertex to the right vertex, with round ends so
    neighbouring segments join smoothly. No fill: the band's coloured shading is drawn underneath."""
    img = Image.new('RGBA', (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    gy = L.GRAPH[1]
    yl, yr = L.db_y(left_db) - gy, L.db_y(right_db) - gy
    d.line((0, yl * SS, w * SS, yr * SS), fill=colour + (alpha,), width=int(width * SS))
    r = width / 2.0
    for x, y in ((0, yl), (w, yr)):
        d.ellipse(((x - r) * SS, (y - r) * SS, (x + r) * SS, (y + r) * SS), fill=colour + (alpha,))
    return img.resize((w, h), Image.LANCZOS)


def line_tiles(prefix, colour, alpha, width, flat_alpha=None):
    gh = L.GRAPH[3]
    w = L.GRAPH[2] // L.LINE_COLS
    th = gh // L.LINE_TILES
    n = L.LINE_LEVELS * len(L.LINE_RISES)
    tiles = [Image.new('RGBA', (w, th * n), (0, 0, 0, 0)) for _ in range(L.LINE_TILES)]
    for level in range(L.LINE_LEVELS):
        for k, r in enumerate(L.LINE_RISES):
            ldb, rdb = -L.DRAW_RANGE_DB + level * L.LINE_STEP_DB, -L.DRAW_RANGE_DB + (level + r) * L.LINE_STEP_DB
            # flat_alpha: a segment lying on 0 dB is drawn faint (the selected band away from its shape)
            a = flat_alpha if flat_alpha is not None and abs(ldb) < 0.2 and abs(rdb) < 0.2 else alpha
            fr = line_frame(w, gh, ldb, rdb, colour, a, width)
            f = level * len(L.LINE_RISES) + k
            for t in range(L.LINE_TILES):
                tiles[t].paste(fr.crop((0, t * th, w, (t + 1) * th)), (0, f * th))
    for t, im in enumerate(tiles):
        im.save(os.path.join(OUT, '%s_t%d.png' % (prefix, t)))
    print('  %s: %d tiles of %d x %d px (%d frames)' % (prefix, L.LINE_TILES, w, th * n, n))


def dot_strips(prefix, colour, alpha, radius, flat_alpha=None):
    """DOTS style: a dot at the column's level (0.5 dB, 73 frames), cut into upper / lower half strips."""
    gh = L.GRAPH[3]
    w = L.GRAPH[2] // L.SHADE_COLS
    gy = L.GRAPH[1]
    upper = Image.new('RGBA', (w, gh // 2 * L.SHADE_FRAMES), (0, 0, 0, 0))
    lower = Image.new('RGBA', (w, gh // 2 * L.SHADE_FRAMES), (0, 0, 0, 0))
    for k in range(L.SHADE_FRAMES):
        img = Image.new('RGBA', (w * SS, gh * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        y = L.db_y(-L.DRAW_RANGE_DB + k * 0.5) - gy
        a = flat_alpha if flat_alpha is not None and k == (L.SHADE_FRAMES - 1) // 2 else alpha   # a dot on 0 dB: faint
        d.ellipse(((w / 2 - radius) * SS, (y - radius) * SS, (w / 2 + radius) * SS, (y + radius) * SS), fill=colour + (a,))
        fr = img.resize((w, gh), Image.LANCZOS)
        upper.paste(fr.crop((0, 0, w, gh // 2)), (0, k * gh // 2))
        lower.paste(fr.crop((0, gh // 2, w, gh)), (0, k * gh // 2))
    upper.save(os.path.join(OUT, '%s_upper.png' % prefix))
    lower.save(os.path.join(OUT, '%s_lower.png' % prefix))


def fill_strip():
    gh = L.GRAPH[3]
    w = L.GRAPH[2] // L.CURVE_COLS
    gy = L.GRAPH[1]
    n = len(L.PARA_COLOURS) * L.FILL_LEVELS
    strip = Image.new('RGBA', (w, gh * n), (0, 0, 0, 0))
    d = ImageDraw.Draw(strip)
    for ci, colour in enumerate(L.PARA_COLOURS):
        for level in range(L.FILL_LEVELS):
            f = ci * L.FILL_LEVELS + level
            db = -L.DRAW_RANGE_DB + level * 0.25
            y0, y1 = L.db_y(0) - gy, L.db_y(db) - gy
            top, bot = sorted((y0, y1))
            if bot - top < 0.5:
                continue   # 0 dB: an empty frame
            d.rectangle((0, f * gh + top, w - 1, f * gh + bot), fill=colour + (int(255 * 0.26),))
    strip.save(os.path.join(OUT, 'para_fill.png'))
    print('  para fill: %d x %d px (%d frames)' % (w, gh * n, n))


def disc(name, size, colour, label=None, ring=False):
    img = Image.new('RGBA', (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if ring:
        for r, a in ((size / 2 - 2, 70), (size / 2 - 6, 255)):
            d.ellipse(((size / 2 - r) * SS, (size / 2 - r) * SS, (size / 2 + r) * SS, (size / 2 + r) * SS),
                      outline=colour + (a,), width=3 * SS)
    else:
        d.ellipse((2 * SS, 2 * SS, (size - 2) * SS, (size - 2) * SS), fill=colour + (240,), outline=(255, 255, 255, 255), width=2 * SS)
    if label:
        f = font(18)
        bb = d.textbbox((0, 0), label, font=f)
        d.text(((size * SS - (bb[2] - bb[0])) / 2 - bb[0], (size * SS - (bb[3] - bb[1])) / 2 - bb[1]), label, font=f, fill=(20, 16, 30, 255))
    img.resize((size, size), Image.LANCZOS).save(os.path.join(OUT, name))


def band_buttons():
    x0, y, w, h, gap = L.BAND_BTN
    for b, colour in enumerate(L.PARA_COLOURS, 1):
        for state in ('on', 'off'):
            img = Image.new('RGBA', (w * SS, h * SS), (0, 0, 0, 0))
            d = ImageDraw.Draw(img)
            fill = colour + (235,) if state == 'on' else L.PANEL + (230,)
            d.rounded_rectangle((SS, SS, (w - 1) * SS, (h - 1) * SS), radius=8 * SS, fill=fill, outline=colour + (255,), width=2 * SS)
            f = font(21)
            s = str(b)
            bb = d.textbbox((0, 0), s, font=f)
            d.text(((w * SS - (bb[2] - bb[0])) / 2 - bb[0], (h * SS - (bb[3] - bb[1])) / 2 - bb[1]), s, font=f,
                   fill=(20, 16, 30, 255) if state == 'on' else colour + (255,))
            img.resize((w, h), Image.LANCZOS).save(os.path.join(OUT, 'pband_%d_%s.png' % (b, state)))


def none_and_count_buttons():
    w, h = L.NONE_BTN[2], L.NONE_BTN[3]
    T.face('NONE', L.PANEL, 0.9, L.GRID_HI, L.TEXT_DIM, w, h, 18).save(os.path.join(OUT, 'pband_none_off.png'))
    T.face('NONE', (230, 230, 240), 0.85, (255, 255, 255), (20, 16, 30), w, h, 18).save(os.path.join(OUT, 'pband_none_on.png'))
    for name, label, r in (('pminus', '\u2212', L.MINUS_BTN), ('pplus', '+', L.PLUS_BTN)):
        T.face(label, L.PANEL, 0.9, L.GRID_HI, L.TEXT, r[2], r[3]).save(os.path.join(OUT, '%s_off.png' % name))
        T.face(label, L.GLOW, 0.55, L.CURVE, (255, 255, 255), r[2], r[3]).save(os.path.join(OUT, '%s_on.png' % name))


def type_segments():
    _, _, w, h, _, _ = L.TYPE_GRID
    for label in L.TYPE_LABELS:
        key = label.lower().replace(' ', '')
        T.face(label, L.PANEL, 0.9, L.GRID_HI, L.TEXT_DIM, w, h, L.TYPE_FONT).save(os.path.join(OUT, 'pseg_%s_off.png' % key))
        T.face(label, L.GLOW, 0.55, L.CURVE, (255, 255, 255), w, h, L.TYPE_FONT).save(os.path.join(OUT, 'pseg_%s_on.png' % key))


def shared_knob_art():
    """GAIN / SLOPE captions (drawn like the bg captions, so they line up)."""
    _, _, w, h = L.SHARED_CAPTION
    for name, cap, colour in (('pcap_gain', 'GAIN', L.TEXT_DIM), ('pcap_slope', 'SLOPE', L.TEXT_DIM)):
        img = Image.new('RGBA', (w * SS, h * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        f = font(16)
        bb = d.textbbox((0, 0), cap, font=f)
        d.text((-bb[0], -bb[1]), cap, font=f, fill=colour + (255,))
        img.resize((w, h), Image.LANCZOS).save(os.path.join(OUT, name + '.png'))


def captions_and_stamps():
    version = L.MANIFEST['version']
    para_caps = [(L.TYPE_GRID[0], L.CAPTION_Y, 'TYPE'), (L.MINUS_BTN[0], L.CAPTION_Y, 'BANDS')]
    para_caps += [(x, L.CAPTION_Y, cap) for _, (x, cap) in L.PARA_KNOBS.items()]
    para_caps += [(x, L.CAPTION_Y, caption) for (caption, kind, _), x in zip(L.BOTTOM, L.BLOCK_X) if kind == 'switch']
    for name, caps in (('bg_para.png', para_caps), ('bg.png', [])):
        path = os.path.join(OUT, name)
        img = Image.open(path).convert('RGBA')
        layer = Image.new('RGBA', (img.width * SS, img.height * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        f = font(16)
        for x, y, cap in caps:
            bb = d.textbbox((0, 0), cap, font=f)
            d.text((x * SS - bb[0], y * SS - bb[1]), cap, font=f, fill=L.TEXT_DIM + (255,))
        s = '%s v%s' % (L.MANIFEST['name'], version)
        fs = font(15)
        bb = d.textbbox((0, 0), s, font=fs)
        d.text(((L.TAB_W - 12) * SS - (bb[2] - bb[0]) - bb[0], L.STAMP_Y * SS - bb[1]), s, font=fs, fill=L.TEXT_DIM + (255,))
        Image.alpha_composite(img, layer.resize(img.size, Image.LANCZOS)).save(path)
    print('  captions; version stamp v%s on bg.png and bg_para.png' % version)


def main():
    if L.LINES:
        line_tiles('para_line', (255, 255, 255), 255, 3.0, flat_alpha=40)
        line_tiles('para_bgline', L.GREY, 200, 2.2)
    else:
        dot_strips('para_dot', (255, 255, 255), 255, 3.4, flat_alpha=60)
        dot_strips('para_bgdot', L.GREY, 200, 2.6)
    fill_strip()
    for b, colour in enumerate(L.PARA_COLOURS, 1):
        disc('para_node_%d.png' % b, L.NODE, colour, str(b))
        disc('para_ring_%d.png' % b, L.RING, colour, ring=True)
    Image.new('RGBA', (L.PEN, L.PEN), (0, 0, 0, 0)).save(os.path.join(OUT, 'para_pen.png'))
    band_buttons()
    none_and_count_buttons()
    type_segments()
    shared_knob_art()
    captions_and_stamps()
    print('  %s: curves, fill, nodes, rings, buttons' % L.VARIANT)


if __name__ == '__main__':
    main()
