"""Shared layout and palette for the EQ Artist skin.

Imported by make_skin_art.py (draws the PNGs) and gen_skins.py (writes TUI.json), so every
rectangle and colour is defined once. Rectangles are (x, y, w, h) in tab coordinates: a tab's
content area is 1280 x 628 and starts 110 px down the MPC's 1280 x 800 screen.
"""
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
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
DOT_FRAMES = 37                               # drawn-curve dot positions: -18..+18 dB in 1 dB steps
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
BLOCK_X = [32, 342, 652, 962]
CAPTION_Y = 492
KNOB = 90                                     # the SDK kit knob (make_ui_kit.py), 127 frames
KNOB_Y = 520
KNOB_VALUE = (100, 540, 190, 46)              # value label, relative to the block's x
SEG_Y, SEG_H = 530, 40
SWITCHES = {                                  # parameter: (captions, segment width)
    'Analyze': (['PRE', 'POST'], 130),
    'Speed': (['SLOW', 'MED', 'FAST'], 92),
}
BOTTOM = [('OUTPUT', 'knob', 'Output'), ('MAX BANDS', 'knob', 'Max Bands'),
          ('ANALYZER', 'switch', 'Analyze'), ('SPEED', 'switch', 'Speed')]


# ---- PARA tab (scripts/make_para_art.py, gen_skins.py; must match plugin/src/ParaPage.hpp and
# core/include/slowops/eqartist/LineStrip.hpp) ---------------------------------------------------------
PARA_COLOURS = [(255, 110, 110), (255, 165, 72), (240, 214, 80), (110, 214, 130),
                (70, 206, 200), (96, 150, 255), (175, 125, 255), (255, 110, 200)]   # bands 1..8
PARA_BANDS = 8
# The PARA line (LineStrip76): 76 columns of 16 px, 0.25 dB vertices, 25 rises (up to 10 dB per column).
LINE_COLS, LINE_LEVELS, LINE_STEP_DB = 76, 145, 0.25
LINE_RISES = [-40, -32, -24, -20, -16, -12, -8, -6, -4, -3, -2, -1, 0, 1, 2, 3, 4, 6, 8, 12, 16, 20, 24, 32, 40]
LINE_TILES = 3                                     # 144 px tiles: 3 per column (6 swamped the MPC: no dragging in LINE)
SHADE_COLS, SHADE_FRAMES = 64, 73                  # DOTS style columns
FILL_LEVELS = 145                                  # band fill: 0.25 dB heights, one frame set per band colour
NODE, RING, PEN = 36, 56, 40
NONE_BTN = (32, 5, 80, 38)
BAND_BTN = (120, 5, 58, 38, 6)                      # x0, y, w, h, gap
PARA_INFO = (944, 5, 304, 38)
PARA_ANALYZER = (816, 5, 118, 38)               # the spectrum on/off (the same switch as DRAW's ANALYZER)
STYLE_SEGS = (646, 5, 78, 38, 6)                # x0, y, w, h, gap: STYLE DOTS | LINE
STYLE_LABELS = ['DOTS', 'LINE']
GREY = (150, 142, 170)                           # the combined curve, behind
PARA_CAPTION_Y = 492
PARA_KNOBS = {'Sel Q': (800, 'Q'), 'Sel Slope': (1010, 'SLOPE')}
BANDS_CAPTION_X = 32
MINUS_BTN = (32, 538, 52, 46)
BANDS_COUNT = (90, 538, 44, 46)
PLUS_BTN = (140, 538, 52, 46)
PARA_KNOB_Y, PARA_VALUE_DX, PARA_VALUE_W = 520, 96, 130
TYPE_LABELS = ['BELL', 'LO SH', 'HI SH', 'LO CUT', 'HI CUT', 'NOTCH']
TYPE_SEGS = (210, 540, 88, 44, 6)                  # x0, y, w, h, gap
STAMP_Y = 604


def node_pad(puck):
    """Bounds of an XYPad whose puck centre must cover the graph: the full width, and vertically the
    curve's +18..-18 dB lines (the DRAW_MARGIN inset), overhanging by half a puck."""
    gx, gy, gw, gh = GRAPH
    o = puck / 2.0
    top, bot = db_y(DRAW_RANGE_DB), db_y(-DRAW_RANGE_DB)
    return (gx - o, top - o, gw + 2 * o, bot - top + 2 * o)
