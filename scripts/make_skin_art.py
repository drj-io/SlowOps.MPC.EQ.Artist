#!/usr/bin/env python3
"""Draw the skin PNGs into plugin/skins/. Standard library only (zlib + struct PNG writer).

The PNGs are committed, so a normal build does not run this. Rerun it when the layout or the
palette in skin_layout.py changes.

  bg.png        1280 x 628   page, graph panel, grid
  spec_bar.png  38 x (432 x 36)  spectrum column film strip: frame k = bar k/35 of the graph tall
  dot.png       38 x (432 x 37)  curve film strip (drawing and response): frame k = a dot at -18 + k dB
  puck.png      40 x 40      pen XYPad puck (a ring)
  draw_dot.png  38 x (432 x 37)  the drawing: a smaller, dimmer dot (frame k = -18 + k dB)

Text (button labels, graph axis labels on bg.png) comes from make_text_art.py (needs Pillow),
which must run after this script.

Film strips stack frames top to bottom and stay under 16384 px tall.
"""
import math
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skin_layout as L  # noqa: E402

OUT = os.path.join(L.ROOT, 'plugin', 'skins')


class Canvas:
    def __init__(self, w, h, fill=(0, 0, 0, 0)):
        self.w, self.h = w, h
        self.px = bytearray(bytes(fill) * (w * h))

    def blend(self, x, y, rgb, a):
        """Source-over one pixel; a in 0..1."""
        if a <= 0 or x < 0 or y < 0 or x >= self.w or y >= self.h:
            return
        a = min(1.0, a)
        i = (y * self.w + x) * 4
        da = self.px[i + 3] / 255.0
        oa = a + da * (1 - a)
        for k in range(3):
            src, dst = rgb[k], self.px[i + k]
            self.px[i + k] = int(round((src * a + dst * da * (1 - a)) / oa)) if oa > 0 else 0
        self.px[i + 3] = int(round(oa * 255))

    def rect(self, x, y, w, h, rgb, a=1.0):
        for yy in range(int(y), int(y + h)):
            for xx in range(int(x), int(x + w)):
                self.blend(xx, yy, rgb, a)

    def disc(self, cx, cy, r, rgb, a=1.0, glow=0.0, glow_rgb=None):
        """Antialiased disc of radius r, with an optional soft glow out to r + glow."""
        reach = r + glow + 1
        for yy in range(int(cy - reach), int(cy + reach) + 1):
            for xx in range(int(cx - reach), int(cx + reach) + 1):
                d = math.hypot(xx + 0.5 - cx, yy + 0.5 - cy)
                if glow > 0 and d > r - 1:
                    g = max(0.0, 1 - (d - r) / glow)
                    self.blend(xx, yy, glow_rgb or rgb, 0.45 * g * g * a)
                cover = min(1.0, max(0.0, r + 0.5 - d))
                self.blend(xx, yy, rgb, cover * a)

    def ring(self, cx, cy, r, width, rgb, a=1.0):
        reach = r + width
        for yy in range(int(cy - reach), int(cy + reach) + 1):
            for xx in range(int(cx - reach), int(cx + reach) + 1):
                d = abs(math.hypot(xx + 0.5 - cx, yy + 0.5 - cy) - r)
                self.blend(xx, yy, rgb, min(1.0, max(0.0, width / 2 + 0.5 - d)) * a)

    def save(self, name):
        raw = bytearray()
        stride = self.w * 4
        for y in range(self.h):
            raw.append(0)
            raw += self.px[y * stride:(y + 1) * stride]

        def chunk(tag, data):
            c = struct.pack('>I', len(data)) + tag + data
            return c + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

        png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', self.w, self.h, 8, 6, 0, 0, 0))
               + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))
        path = os.path.join(OUT, name)
        open(path, 'wb').write(png)
        print('  %-14s %5d x %-6d %7.1f KB' % (name, self.w, self.h, len(png) / 1024.0))


def background():
    c = Canvas(L.TAB_W, L.TAB_H, L.BG + (255,))
    gx, gy, gw, gh = L.GRAPH
    c.rect(gx, gy, gw, gh, L.PANEL)
    for f in (30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
              1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000):
        major = f in (100, 1000, 10000)
        c.rect(round(L.freq_x(f)), gy, 1, gh, L.GRID_HI if major else L.GRID, 0.8 if major else 0.6)
    for db in L.DB_LINES:
        y = round(L.db_y(db))
        c.rect(gx, y, gw, 2 if db == 0 else 1, L.GRID_HI if db == 0 else L.GRID, 1.0 if db == 0 else 0.7)
    for x, y, w, h in ((gx - 1, gy - 1, gw + 2, 1), (gx - 1, gy + gh, gw + 2, 1),
                       (gx - 1, gy - 1, 1, gh + 2), (gx + gw, gy - 1, 1, gh + 2)):
        c.rect(x, y, w, h, L.GRID_HI)
    c.save('bg.png')


def spec_bar():
    w, h, n = L.COL_W, L.GRAPH[3], L.SPEC_FRAMES
    c = Canvas(w, h * n)
    for k in range(1, n):
        top = h - round(h * k / (n - 1))
        for y in range(top, h):
            t = (y - top) / max(1, h - top)              # 0 at the bar's top, 1 at the bottom
            a = 0.62 - 0.4 * t
            for x in range(3, w - 3):
                c.blend(x, k * h + y, L.SPEC, a)
        c.rect(3, k * h + top, w - 6, 2, L.CURVE, 0.55)   # bright cap
    c.save('spec_bar.png')


def dot():
    w, h, n = L.COL_W, L.GRAPH[3], L.DOT_FRAMES
    c = Canvas(w, h * n)
    gy = L.GRAPH[1]
    for k in range(n):
        db = -L.DRAW_RANGE_DB + k * (2 * L.DRAW_RANGE_DB / (n - 1))
        cy = L.db_y(db) - gy
        c.disc(w / 2, k * h + cy, 6.5, L.CURVE, 1.0, glow=9, glow_rgb=L.GLOW)
    c.save('dot.png')


def puck():
    c = Canvas(L.PUCK, L.PUCK)
    c.ring(L.PUCK / 2, L.PUCK / 2, 15, 3, L.CURVE, 0.9)
    c.disc(L.PUCK / 2, L.PUCK / 2, 3, L.CURVE, 0.9)
    c.save('puck.png')


def draw_dot():
    w, h, n = L.COL_W, L.GRAPH[3], L.DOT_FRAMES
    c = Canvas(w, h * n)
    gy = L.GRAPH[1]
    for k in range(n):
        db = -L.DRAW_RANGE_DB + k * (2 * L.DRAW_RANGE_DB / (n - 1))
        c.ring(w / 2, k * h + L.db_y(db) - gy, 5.0, 1.6, L.DRAWN, 0.9)
    c.save('draw_dot.png')


def main():
    os.makedirs(OUT, exist_ok=True)
    print('skin art -> %s' % OUT)
    background()
    spec_bar()
    dot()
    puck()
    draw_dot()


if __name__ == '__main__':
    main()
