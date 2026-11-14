#!/usr/bin/env python3
"""Draw everything with text into plugin/skins/ (needs Pillow): the labelled buttons and switch
segments, and the graph's frequency / dB labels and the bottom-row captions onto bg.png.

On the MPC, fixed-text Label widgets do not show, and text cannot sit over an image button, so
all fixed text is drawn into the images. The font is Titillium Web Bold (SIL OFL 1.1,
assets/fonts/). The PNGs are committed, so a normal build does not need Pillow. Rerun this
after make_skin_art.py (which redraws bg.png without text):

    python3 -m venv /tmp/venv && /tmp/venv/bin/pip install pillow
    python3 scripts/make_skin_art.py && /tmp/venv/bin/python scripts/make_text_art.py
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skin_layout as L  # noqa: E402

OUT = os.path.join(L.ROOT, 'plugin', 'skins')
FONT = os.path.join(L.ROOT, 'assets', 'fonts', 'TitilliumWeb-Bold.ttf')
SS = 4   # supersampling


def face(label, fill, fill_a, edge, text, w=L.BUTTON_W, h=L.BUTTON_H):
    img = Image.new('RGBA', (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((SS, SS, w * SS - SS, h * SS - SS), radius=8 * SS,
                        fill=fill + (int(255 * fill_a),), outline=edge + (255,), width=2 * SS)
    font = ImageFont.truetype(FONT, 21 * SS)
    box = d.textbbox((0, 0), label, font=font)
    tw, th = box[2] - box[0], box[3] - box[1]
    d.text(((w * SS - tw) / 2 - box[0], (h * SS - th) / 2 - box[1]), label, font=font, fill=text + (255,))
    return img.resize((w, h), Image.LANCZOS)


def axis_labels():
    path = os.path.join(OUT, 'bg.png')
    img = Image.open(path).convert('RGBA')
    big = img.resize((img.width * SS, img.height * SS), Image.NEAREST)
    layer = Image.new('RGBA', big.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    font = ImageFont.truetype(FONT, 15 * SS)
    gx, gy, gw, gh = L.GRAPH
    for f, s in L.FREQ_LABELS:
        box = d.textbbox((0, 0), s, font=font)
        d.text((L.freq_x(f) * SS - (box[2] - box[0]) / 2 - box[0], (gy + gh - 20) * SS - box[1]), s, font=font,
               fill=L.TEXT_DIM + (255,))
    for db in (12, 0, -12):
        s = '%+d' % db if db else '0'
        box = d.textbbox((0, 0), s, font=font)
        d.text(((gx + 6) * SS - box[0], (L.db_y(db) - 20) * SS - box[1]), s, font=font, fill=L.TEXT_DIM + (255,))
    small = layer.resize(img.size, Image.LANCZOS)
    Image.alpha_composite(img, small).save(path)
    print('  bg.png: axis labels')


def captions():
    """The bottom row's captions, onto bg.png."""
    path = os.path.join(OUT, 'bg.png')
    img = Image.open(path).convert('RGBA')
    layer = Image.new('RGBA', (img.width * SS, img.height * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    font = ImageFont.truetype(FONT, 16 * SS)
    for (caption, _, _), x in zip(L.BOTTOM, L.BLOCK_X):
        box = d.textbbox((0, 0), caption, font=font)
        d.text((x * SS - box[0], L.CAPTION_Y * SS - box[1]), caption, font=font, fill=L.TEXT_DIM + (255,))
    Image.alpha_composite(img, layer.resize(img.size, Image.LANCZOS)).save(path)
    print('  bg.png: bottom-row captions')


def main():
    axis_labels()
    captions()
    for name, label in L.BUTTONS:
        hot = L.WARN if name == 'Bypass' else L.GLOW
        face(label, L.PANEL, 0.9, L.GRID_HI, L.TEXT).save(os.path.join(OUT, '%s_off.png' % label.lower()))
        face(label, hot, 0.55, L.CURVE, (255, 255, 255)).save(os.path.join(OUT, '%s_on.png' % label.lower()))
        print('  %s_on.png / %s_off.png' % (label.lower(), label.lower()))
    for param, (labels, w) in L.SWITCHES.items():
        for label in labels:
            h = 56 if param == 'Display' else L.SEG_H
            face(label, L.PANEL, 0.9, L.GRID_HI, L.TEXT_DIM, w, h).save(os.path.join(OUT, 'seg_%s_off.png' % label.lower()))
            face(label, L.GLOW, 0.55, L.CURVE, (255, 255, 255), w, h).save(os.path.join(OUT, 'seg_%s_on.png' % label.lower()))
            print('  seg_%s_on.png / seg_%s_off.png' % (label.lower(), label.lower()))


if __name__ == '__main__':
    main()
