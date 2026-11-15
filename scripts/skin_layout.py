"""Shared layout and palette for the EQ Artist skins (both plugins).

Imported by the art scripts (which draw the PNGs) and gen_skins.py (which writes TUI.json), so every
rectangle and colour is defined once. Rectangles are (x, y, w, h) in tab coordinates: a tab's
content area is 1280 x 628 and starts 110 px down the MPC's 1280 x 800 screen.

The plugin to work on comes from the environment: EQA_VARIANT=dots (default) or lines. It decides the
output folder (plugins/<variant>/skins) and how curves are drawn (LINES, CURVE_COLS).
"""
import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VARIANT = os.environ.get('EQA_VARIANT', 'dots')
assert VARIANT in ('dots', 'lines'), 'EQA_VARIANT must be dots or lines'
LINES = VARIANT == 'lines'
PROJECT = os.path.join(ROOT, 'plugins', VARIANT)
SKINS = os.path.join(PROJECT, 'skins')
MANIFEST = json.load(open(os.path.join(PROJECT, 'plugin.json')))
TAB_W, TAB_H = 1280, 628

# ---- palette: deep purple, to match the repo's editor theme --------------------------------
BG = (30, 14, 42)            # page
PANEL = (42, 22, 62)         # graph panel
GRID = (64, 40, 88)          # minor grid
GRID_HI = (112, 72, 143)     # 0 dB line, panel outline
SPEC = (153, 113, 192)       # spectrum bars
CURVE = (236, 222, 255)      # curve dots, button outlines
GLOW = (190, 140, 255)       # glow around the curve
TEXT = (204, 187, 221)
TEXT_DIM = (150, 120, 180)
DRAWN = (176, 150, 214)      # your drawing (small dim dots)
WARN = (220, 60, 90)         # Bypass on


def argb(rgb, a=255):
    """Colour for TUI.json: AARRGGBB."""
    return '%02x%02x%02x%02x' % (a, rgb[0], rgb[1], rgb[2])


# ---- graph ----------------------------------------------------------------------------------
NUM_COLS = 32
COL_W = 38
GRAPH = (32, 48, NUM_COLS * COL_W, 432)      # 1216 x 432
F_LO, F_HI = 20.0, 20000.0
DRAW_RANGE_DB = 18.0
DRAW_MARGIN = 12                              # the curve's top and bottom inset
SPEC_FRAMES = 36                              # spectrum bar heights (must match EqArtist.cpp)
DOT_FRAMES = 37                               # the drawing's ring positions: -18..+18 dB in 1 dB steps
PUCK = 40


def freq_x(f):
    gx, _, gw, _ = GRAPH
    return gx + gw * math.log(f / F_LO) / math.log(F_HI / F_LO)


def db_y(db):
    """Centre y of the curve dot at db."""
    _, gy, _, gh = GRAPH
    travel = gh - 2 * DRAW_MARGIN
    return gy + DRAW_MARGIN + travel * (0.5 - db / (2 * DRAW_RANGE_DB))


FREQ_LABELS = [(50, '50'), (100, '100'), (200, '200'), (500, '500'), (1000, '1k'), (2000, '2k'),
               (5000, '5k'), (10000, '10k')]
DB_LINES = [-12, -6, 0, 6, 12]

# ---- EQ tab: buttons and status (top strip), controls (bottom row) ---------------------------
BUTTON_W, BUTTON_H = 170, 38
BUTTON_Y = 5
BUTTONS = [('Clear', 'CLEAR'), ('Undo', 'UNDO'), ('Bypass', 'BYPASS'), ('Analyzer', 'ANALYZER')]
BUTTON_X = [32, 216, 400, 584]
FIT_INFO = (780, 5, 468, 38)

# Bottom row: four blocks, each with its caption drawn into bg.png above it.
BLOCK_X = [292, 652, 962]                         # BANDS at PARA's BANDS place; ANALYZER, SPEED
CAPTION_Y = 492
KNOB = 90                                     # the SDK kit knob (make_ui_kit.py), 127 frames
KNOB_Y = 520
KNOB_VALUE = (100, 540, 190, 46)              # value label, relative to the block's x
SEG_Y, SEG_H = 530, 40
SWITCHES = {                                  # parameter: (captions, segment width)
    'Analyze': (['PRE', 'POST'], 130),
    'Speed': (['SLOW', 'MED', 'FAST'], 92),
}
# (Output, parameter 0, has no control: the MPC nudges parameter 0 on every load, so it must stay harmless.)
BOTTOM = [('BANDS', 'bands', 'Max Bands'), ('ANALYZER', 'switch', 'Analyze'), ('SPEED', 'switch', 'Speed')]


# ---- PARA tab and the curves (scripts/make_para_art.py, gen_skins.py; must match
# plugin-common/ParaPage.hpp and core/include/slowops/eqartist/LineStrip.hpp) ------------------------
PARA_COLOURS = [(255, 110, 110), (255, 165, 72), (240, 214, 80), (110, 214, 130),
                (70, 206, 200), (96, 150, 255), (175, 125, 255), (255, 110, 200)]   # bands 1..8
PARA_BANDS = 8
# The PARA line (LineStrip76): 76 columns of 16 px, 0.25 dB vertices, 25 rises (up to 10 dB per column).
LINE_COLS, LINE_LEVELS, LINE_STEP_DB = 76, 145, 0.25
LINE_RISES = [-40, -32, -24, -20, -16, -12, -8, -6, -4, -3, -2, -1, 0, 1, 2, 3, 4, 6, 8, 12, 16, 20, 24, 32, 40]
LINE_TILES = 3                                     # 144 px tiles: 3 per column (6 swamped the MPC: no dragging in LINE)
SHADE_COLS, SHADE_FRAMES = 64, 73                  # dots: 64 columns of 19 px, 0.5 dB
CURVE_COLS = LINE_COLS if LINES else SHADE_COLS    # columns of this plugin's curves and fill
FILL_LEVELS = 145                                  # band fill: 0.25 dB heights, one frame set per band colour
NODE, RING, PEN = 36, 56, 40
# PARA's top row: DRAW's CLEAR / UNDO / BYPASS / ANALYZER in the same place (BUTTONS at BUTTON_X), then the
# band selection on the right. PARA's status sits at the bottom, under ANALYZER PRE | POST, by the version stamp.
NONE_BTN = (776, 5, 64, 38)
BAND_BTN = (845, 5, 46, 38, 5)                      # x0, y, w, h, gap: 845 .. 1248
PARA_INFO = (652, 594, 440, 30)
GREY = (150, 142, 170)                           # the combined curve, behind
# PARA's bottom row, captions on DRAW's caption line (CAPTION_Y). The right half is DRAW's analyzer settings in
# the same place (ANALYZER PRE | POST, SPEED SLOW | MED | FAST: BOTTOM[1:] at BLOCK_X[1:]). The left half:
# TYPE as a 3 x 2 grid, BANDS - n +, then the Q and SLOPE knobs with their values underneath.
TYPE_LABELS = ['BELL', 'LO SH', 'HI SH', 'LO CUT', 'HI CUT', 'NOTCH']
TYPE_GRID = (32, 514, 76, 38, 6, 3)                # x0, y0, w, h, gap, columns: 32 .. 272, 514 .. 596
TYPE_FONT = 18
TYPE_ORDER = [0, 1, 2, 5, 3, 4]                    # grid slot -> type (BandType order): BELL LO SH HI SH / NOTCH LO CUT HI CUT
MINUS_BTN = (292, 530, 38, 40)                     # BANDS: 292 .. 408, on DRAW's switch line (SEG_Y)
BANDS_COUNT = (332, 530, 36, 40)
PLUS_BTN = (370, 530, 38, 40)
PARA_KNOBS = {'Sel Q': (430, 'Q')}                # knob x: 430 .. 520
# One knob for GAIN or SLOPE (no band type has both): GAIN for bell, shelves and notch (its depth), SLOPE for the
# cuts. Its caption is an image that swaps with it (the bg captions cannot change).
SHARED_X = 538                                    # knob x: 538 .. 628
SHARED_CAPTION = (538, 492, 96, 20)
PARA_KNOB_Y = 510
PARA_VALUE = (-7, 600, 104, 26)                    # value label under the knob: dx, y, w, h
STAMP_Y = 604

# Focus slots: each control group is one hardware-focus stop. Touch it (or move to it with the cursor keys) and the
# MPC draws a ring around the slot, and the data wheel edits its parameter. Rects in tab coordinates.
FOCUS_BG, FOCUS_RING = argb((255, 255, 255), 0x19), argb(GLOW)
# Every bottom-row ring has the same height, from under the graph to the bottom edge (the tallest group: a knob
# with its value underneath).
FOCUS_Y, FOCUS_H = 480, 148
FOCUS_KNOB_SLOT = (-6, FOCUS_Y, 300, FOCUS_H)       # a knob block in BOTTOM (none at the moment): dx, y, w, h
FOCUS_SWITCH_Y, FOCUS_SWITCH_H = FOCUS_Y, FOCUS_H   # PRE | POST, SPEED: around the segments and their caption
PARA_FOCUS = {'Sel Type': (26, FOCUS_Y, 252, FOCUS_H), 'Bands': (286, FOCUS_Y, 128, FOCUS_H),
              'Sel Q': (422, FOCUS_Y, 106, FOCUS_H), 'Shared': (530, FOCUS_Y, 106, FOCUS_H), 'Sel Band': (772, 1, 480, 46)}


def node_pad(puck):
    """Bounds of an XYPad whose puck centre must cover the graph: the full width, and vertically the
    curve's +18..-18 dB lines (the DRAW_MARGIN inset), overhanging by half a puck."""
    gx, gy, gw, gh = GRAPH
    o = puck / 2.0
    top, bot = db_y(DRAW_RANGE_DB), db_y(-DRAW_RANGE_DB)
    return (gx - o, top - o, gw + 2 * o, bot - top + 2 * o)
