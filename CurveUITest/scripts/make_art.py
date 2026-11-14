#!/usr/bin/env python3
"""Draw the Curve UI Test skin images into skins/ (needs Pillow).

Fixed-text Label widgets do not render on the MPC (seen with EQ Artist on a Live III), so every caption is
drawn into the tab backgrounds. Run with a Python that has Pillow:
    python3 -m venv /tmp/venv && /tmp/venv/bin/pip install pillow && /tmp/venv/bin/python scripts/make_art.py
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout as L  # noqa: E402

OUT = os.path.join(L.ROOT, 'skins')
SS = 3


def font(px):
    return ImageFont.truetype(L.FONT, px * SS)


def centred(d, rect, s, f, fill):
    x, y, w, h = rect
    b = d.textbbox((0, 0), s, font=f)
    d.text(((x + w / 2) * SS - (b[2] - b[0]) / 2 - b[0], (y + h / 2) * SS - (b[3] - b[1]) / 2 - b[1]), s, font=f, fill=fill)


def pad_panel(d, rect, colour, label):
    """The pad's area: panel, 10 % grid, centre cross, corner ticks, and a label in the corner."""
    x, y, w, h = rect
    d.rectangle((x * SS, y * SS, (x + w) * SS, (y + h) * SS), fill=L.PANEL + (255,), outline=colour + (255,), width=2 * SS)
    for k in range(1, 10):
        c = L.GRID_HI if k == 5 else L.GRID
        d.line(((x + w * k / 10) * SS, y * SS, (x + w * k / 10) * SS, (y + h) * SS), fill=c + (255,), width=SS)
        d.line((x * SS, (y + h * k / 10) * SS, (x + w) * SS, (y + h * k / 10) * SS), fill=c + (255,), width=SS)
    f = font(16)
    d.text(((x + 8) * SS, (y + 6) * SS), label, font=f, fill=colour + (255,))


def background(name, readouts, pads):
    img = Image.new('RGBA', (L.TAB_W * SS, L.TAB_H * SS), L.BG + (255,))
    d = ImageDraw.Draw(img)
    f = font(14)
    for row, items in enumerate(readouts):
        for i, (cap, _) in enumerate(items):
            (cx, cy, cw, ch), _ = L.readout_rects(row, i)
            d.text((cx * SS, cy * SS), cap, font=f, fill=L.TEXT_DIM + (255,))
    for rect, colour, label in pads:
        pad_panel(d, rect, colour, label)
    img.resize((L.TAB_W, L.TAB_H), Image.LANCZOS).save(os.path.join(OUT, 'bg_%s.png' % name.lower()))


def puck(letter, colour):
    s = L.PUCK
    img = Image.new('RGBA', (s * SS, s * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse((2 * SS, 2 * SS, (s - 2) * SS, (s - 2) * SS), fill=colour + (70,), outline=colour + (255,), width=3 * SS)
    d.line(((s / 2) * SS, 8 * SS, (s / 2) * SS, (s - 8) * SS), fill=colour + (160,), width=SS)
    d.line((8 * SS, (s / 2) * SS, (s - 8) * SS, (s / 2) * SS), fill=colour + (160,), width=SS)
    centred(d, (0, 0, s, s), letter, font(20), (255, 255, 255, 255))
    img.resize((s, s), Image.LANCZOS).save(os.path.join(OUT, 'puck_%s.png' % letter.lower()))


def button(name, label):
    for state, fill, a, text in (('off', L.PANEL, 255, L.TEXT), ('on', L.ACCENT, 200, (20, 20, 30))):
        img = Image.new('RGBA', (L.BUTTON_W * SS, L.BUTTON_H * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        d.rounded_rectangle((SS, SS, (L.BUTTON_W - 1) * SS, (L.BUTTON_H - 1) * SS), radius=8 * SS,
                            fill=fill + (a,), outline=L.ACCENT + (255,), width=2 * SS)
        centred(d, (0, 0, L.BUTTON_W, L.BUTTON_H), label, font(19), text + (255,))
        img.resize((L.BUTTON_W, L.BUTTON_H), Image.LANCZOS).save(os.path.join(OUT, '%s_%s.png' % (name, state)))


def graph_panel(d):
    gx, gy, gw, gh = L.GRAPH
    d.rectangle((gx * SS, gy * SS, (gx + gw) * SS, (gy + gh) * SS), fill=L.PANEL + (255,), outline=L.GRID_HI + (255,), width=SS)
    import math
    for f in (50, 100, 200, 500, 1000, 2000, 5000, 10000):
        x = gx + gw * math.log(f / 20) / math.log(1000)
        d.line((x * SS, gy * SS, x * SS, (gy + gh) * SS), fill=(L.GRID_HI if f in (100, 1000, 10000) else L.GRID) + (255,), width=SS)
    for db in (-12, -6, 0, 6, 12):
        y = L.db_y(db)
        d.line((gx * SS, y * SS, (gx + gw) * SS, y * SS), fill=(L.GRID_HI if db == 0 else L.GRID) + (255,), width=(2 if db == 0 else 1) * SS)
    f = font(14)
    for fr, s in ((50, '50'), (100, '100'), (200, '200'), (500, '500'), (1000, '1k'), (2000, '2k'), (5000, '5k'), (10000, '10k')):
        x = gx + gw * math.log(fr / 20) / math.log(1000)
        centred(d, (x - 30, gy + gh - 22, 60, 18), s, f, L.TEXT_DIM + (255,))
    for db in (12, 0, -12):
        d.text(((gx + 6) * SS, (L.db_y(db) - 19) * SS), '%+d' % db if db else '0', font=f, fill=L.TEXT_DIM + (255,))


def curve_frame(w, h, db, fill, fill_a, dot):
    """One full-height column frame: a fill from 0 dB to db, plus (optionally) a dot at db."""
    img = Image.new('RGBA', (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    gy = L.GRAPH[1]
    y0, y1 = L.db_y(0) - gy, L.db_y(db) - gy
    top, bot = sorted((y0, y1))
    d.rectangle((SS, top * SS, (w - 1) * SS, bot * SS), fill=fill + (int(255 * fill_a),))
    if dot:
        r = 3.6
        d.ellipse(((w / 2 - r) * SS, (y1 - r) * SS, (w / 2 + r) * SS, (y1 + r) * SS), fill=L.CURVE + (255,))
    return img.resize((w, h), Image.LANCZOS)


def strips():
    gh = L.GRAPH[3]
    w = L.GRAPH[2] // L.CURVE_COLS
    frames = [curve_frame(w, gh, -L.RANGE_DB + k * (2 * L.RANGE_DB) / (L.CURVE_FRAMES - 1), (150, 150, 200), 0.32, True)
              for k in range(L.CURVE_FRAMES)]
    tall = Image.new('RGBA', (w, gh * L.CURVE_FRAMES), (0, 0, 0, 0))
    upper = Image.new('RGBA', (w, gh // 2 * L.CURVE_FRAMES), (0, 0, 0, 0))
    lower = Image.new('RGBA', (w, gh // 2 * L.CURVE_FRAMES), (0, 0, 0, 0))
    for k, fr in enumerate(frames):
        tall.paste(fr, (0, k * gh))
        upper.paste(fr.crop((0, 0, w, gh // 2)), (0, k * gh // 2))
        lower.paste(fr.crop((0, gh // 2, w, gh)), (0, k * gh // 2))
    tall.save(os.path.join(OUT, 'curve_tall.png'))
    upper.save(os.path.join(OUT, 'curve_upper.png'))
    lower.save(os.path.join(OUT, 'curve_lower.png'))
    sw = L.GRAPH[2] // L.SHADE_COLS
    shade = Image.new('RGBA', (sw, gh * L.SHADE_FRAMES), (0, 0, 0, 0))
    for k in range(L.SHADE_FRAMES):
        shade.paste(curve_frame(sw, gh, -L.RANGE_DB + k, L.BELL, 0.28, False), (0, k * gh))
    shade.save(os.path.join(OUT, 'shade.png'))
    print('  strips: curve_upper/lower %dx%d, curve_tall %dx%d, shade %dx%d' % (
        w, upper.height, w, tall.height, sw, shade.height))


def line_frame(w, h, left_db, right_db):
    """A column frame for LINE mode: the segment from the left boundary (left_db) to the right boundary
    (right_db), with round ends so neighbouring segments join smoothly, and the fill under it."""
    img = Image.new('RGBA', (w * SS, h * SS), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)
    gy = L.GRAPH[1]
    yl, yr, y0 = L.db_y(left_db) - gy, L.db_y(right_db) - gy, L.db_y(0) - gy
    dr.polygon([(0, yl * SS), (w * SS, yr * SS), (w * SS, y0 * SS), (0, y0 * SS)], fill=(150, 150, 200, int(255 * 0.30)))
    dr.line((0, yl * SS, w * SS, yr * SS), fill=L.CURVE + (255,), width=int(2.6 * SS))
    r = 1.3
    for x, y in ((0, yl), (w, yr)):
        dr.ellipse(((x - r) * SS, (y - r) * SS, (x + r) * SS, (y + r) * SS), fill=L.CURVE + (255,))
    return img.resize((w, h), Image.LANCZOS)


def line_tiles():
    gh = L.GRAPH[3]
    w = L.GRAPH[2] // L.LINE_COLS
    th = gh // L.LINE_TILES
    rises = len(L.LINE_RISES)
    n = L.LINE_LEVELS * rises
    tiles = [Image.new('RGBA', (w, th * n), (0, 0, 0, 0)) for _ in range(L.LINE_TILES)]
    step = L.LINE_STEP_DB
    for level in range(L.LINE_LEVELS):
        for k, r in enumerate(L.LINE_RISES):
            right = level + r   # may run off the graph; only reachable frames are ever selected
            fr = line_frame(w, gh, -L.RANGE_DB + level * step, -L.RANGE_DB + right * step)
            f = level * rises + k
            for t in range(L.LINE_TILES):
                tiles[t].paste(fr.crop((0, t * th, w, (t + 1) * th)), (0, f * th))
    for t, im in enumerate(tiles):
        im.save(os.path.join(OUT, 'line_t%d.png' % t))
    print('  line tiles: %d x %d x %d px (%d frames)' % (L.LINE_TILES, w, th * n, n))


def node():
    s = L.NODE
    img = Image.new('RGBA', (s * SS, s * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse((2 * SS, 2 * SS, (s - 2) * SS, (s - 2) * SS), fill=L.BELL + (235,), outline=(255, 255, 255, 255), width=2 * SS)
    centred(d, (0, 0, s, s), '1', font(20), (255, 255, 255, 255))
    img.resize((s, s), Image.LANCZOS).save(os.path.join(OUT, 'node_1.png'))


def segment(label, w, h):
    for state, fill, a, text in (('off', L.PANEL, 255, L.TEXT), ('on', L.ACCENT, 210, (20, 20, 30))):
        img = Image.new('RGBA', (w * SS, h * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        d.rounded_rectangle((SS, SS, (w - 1) * SS, (h - 1) * SS), radius=7 * SS, fill=fill + (a,), outline=L.ACCENT + (255,), width=2 * SS)
        centred(d, (0, 0, w, h), label, font(18), text + (255,))
        img.resize((w, h), Image.LANCZOS).save(os.path.join(OUT, 'seg_%s_%s.png' % (label.lower(), state)))


def curve_background():
    img = Image.new('RGBA', (L.TAB_W * SS, L.TAB_H * SS), L.BG + (255,))
    d = ImageDraw.Draw(img)
    f = font(14)
    for i, (cap, _) in enumerate(L.CURVE_READOUTS):
        (cx, cy, cw, ch), _ = L.readout_rects(0, i)
        d.text((cx * SS, cy * SS), cap, font=f, fill=L.TEXT_DIM + (255,))
    graph_panel(d)
    for cap, x in (('Q', L.Q_KNOB[0]), ('STRIP', L.STRIP_SEGS[1]), ('REFRESH (UPDATES PER 1/30 S)', L.REFRESH_SEGS[1]),
                   ('SWEEP', L.SWEEP_BUTTON[0])):
        d.text((x * SS, L.BOTTOM_CAPTION_Y * SS), cap, font=f, fill=L.TEXT_DIM + (255,))
    img.resize((L.TAB_W, L.TAB_H), Image.LANCZOS).save(os.path.join(OUT, 'bg_curve.png'))


def lines_background():
    img = Image.new('RGBA', (L.TAB_W * SS, L.TAB_H * SS), L.BG + (255,))
    d = ImageDraw.Draw(img)
    f = font(14)
    for (x, y, w, h), cap in zip(L.PATH_BOXES, L.PATH_CAPTIONS):
        d.rectangle((x * SS, y * SS, (x + w) * SS, (y + h) * SS), outline=L.GRID_HI + (255,), width=SS)
        d.text((x * SS, (y - 20) * SS), cap, font=f, fill=L.TEXT_DIM + (255,))
    x, y, w, h = L.PLOTTER_BOX
    d.rectangle((x * SS, y * SS, (x + w) * SS, (y + h) * SS), outline=L.GRID_HI + (255,), width=SS)
    d.text((x * SS, (y - 20) * SS), 'PLOTTER: LINE VALUE, WITH HISTORY', font=f, fill=L.TEXT_DIM + (255,))
    d.text((L.LINE_VALUE_LABEL[0] * SS, (L.LINE_VALUE_LABEL[1] - 18) * SS), 'LINE VALUE', font=f, fill=L.TEXT_DIM + (255,))
    img.resize((L.TAB_W, L.TAB_H), Image.LANCZOS).save(os.path.join(OUT, 'bg_lines.png'))


def stamp_version():
    """Draw "Curve UI Test vX.Y.Z" (from plugin.json) into the bottom-right corner of every tab background,
    so it is obvious on the MPC which build is loaded (the app keeps the old one until it restarts)."""
    import json
    version = json.load(open(os.path.join(L.ROOT, 'plugin.json')))['version']
    for name in ('full', 'split', 'stack', 'safe', 'curve', 'lines', 'env', 'env2'):
        path = os.path.join(OUT, 'bg_%s.png' % name)
        img = Image.open(path).convert('RGBA')
        big = Image.new('RGBA', (img.width * SS, img.height * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(big)
        f = font(15)
        s = 'Curve UI Test v%s' % version
        b = d.textbbox((0, 0), s, font=f)
        d.text(((L.TAB_W - 12) * SS - (b[2] - b[0]) - b[0], 604 * SS - b[1]), s, font=f, fill=L.ACCENT + (255,))
        Image.alpha_composite(img, big.resize(img.size, Image.LANCZOS)).save(path)
    print('  version stamp: v%s' % version)


def env_background():
    img = Image.new('RGBA', (L.TAB_W * SS, L.TAB_H * SS), L.BG + (255,))
    d = ImageDraw.Draw(img)
    f = font(14)
    for key, (x, y, w, h) in L.ENV_BOXES.items():
        d.rectangle(((x - 1) * SS, (y - 1) * SS, (x + w + 1) * SS, (y + h + 1) * SS), outline=L.GRID_HI + (255,), width=SS)
        d.text((x * SS, (y - 20) * SS), L.ENV_CAPTIONS[key], font=f, fill=L.TEXT_DIM + (255,))
    for i, (cap, _) in enumerate(L.ENV_READOUTS):
        x, y = 680 + (i % 2) * 280, 440 + (i // 2) * 70
        d.text((x * SS, y * SS), cap, font=f, fill=L.TEXT_DIM + (255,))
    d.text((L.ENV_SWEEP_BUTTON[0] * SS, (L.ENV_SWEEP_BUTTON[1] - 20) * SS), 'SWEEP (THE PLUGIN MOVES EVERY POINT)', font=f,
           fill=L.TEXT_DIM + (255,))
    img.resize((L.TAB_W, L.TAB_H), Image.LANCZOS).save(os.path.join(OUT, 'bg_env.png'))


def env2_background():
    img = Image.new('RGBA', (L.TAB_W * SS, L.TAB_H * SS), L.BG + (255,))
    d = ImageDraw.Draw(img)
    f = font(13)
    x0, y0, w, h = L.RULER_ENV
    d.text((x0 * SS, (y0 - 20) * SS), 'PITCH ENVELOPE, HIDDEN HANDLES / SCALE / BACKGROUND: POINTS ZIGZAG 10 % / 90 %, EVERY STAGE = WIDTH',
           font=f, fill=L.TEXT_DIM + (255,))
    for px in range(0, w + 1, 25):   # ruler: a tick every 25 px, a label every 100 px (px from the box's left edge)
        big = px % 100 == 0
        d.line(((x0 + px) * SS, y0 * SS, (x0 + px) * SS, (y0 + h) * SS), fill=(L.GRID_HI if big else L.GRID) + (255,), width=SS)
        if big:
            d.text(((x0 + px + 3) * SS, L.RULER_TICKS_Y * SS), str(px), font=f, fill=L.TEXT_DIM + (255,))
    for frac in (0.1, 0.5, 0.9):
        y = y0 + h * (1 - frac)
        d.line((x0 * SS, y * SS, (x0 + w) * SS, y * SS), fill=L.GRID + (255,), width=SS)
        d.text(((x0 - 28) * SS, (y - 8) * SS), '%d%%' % round(frac * 100), font=f, fill=L.TEXT_DIM + (255,))
    for key, (x, y, w2, h2) in L.BEND_ENVS.items():
        d.rectangle(((x - 1) * SS, (y - 1) * SS, (x + w2 + 1) * SS, (y + h2 + 1) * SS), outline=L.GRID_HI + (255,), width=SS)
        d.text((x * SS, (y - 20) * SS), '%s, CURVE TYPE EXPONENTIAL, SHAPES = BEND, SHAPE HANDLES SHOWN' % key.upper(), font=f,
               fill=L.TEXT_DIM + (255,))
    img.resize((L.TAB_W, L.TAB_H), Image.LANCZOS).save(os.path.join(OUT, 'bg_env2.png'))


def main():
    os.makedirs(OUT, exist_ok=True)
    strips()
    line_tiles()
    node()
    for labels, _, w in (L.STRIP_SEGS, L.REFRESH_SEGS):
        for lab in labels:
            segment(lab, w, L.SEG_H)
    button('sweep', 'SWEEP')
    button('linesweep', 'SWEEP')
    curve_background()
    lines_background()
    env_background()
    env2_background()
    button('envsweep', 'SWEEP')
    tabs = dict(L.TABS)
    background('FULL', tabs['FULL'], [(L.FULL_PAD, L.PADS['A'], 'PAD A')])
    background('SPLIT', tabs['SPLIT'], [(L.SPLIT_PADS['B'], L.PADS['B'], 'PAD B'), (L.SPLIT_PADS['C'], L.PADS['C'], 'PAD C')])
    background('STACK', tabs['STACK'], [(L.STACK_PAD, L.PADS['E'], 'PADS D (under) + E (on top)')])
    background('SAFE', [], [])
    for letter, colour in L.PADS.items():
        puck(letter, colour)
    button('reset', 'RESET')
    button('automove', 'AUTO MOVE')
    stamp_version()
    print('art -> %s: %s' % (OUT, ', '.join(sorted(os.listdir(OUT)))))


if __name__ == '__main__':
    main()
