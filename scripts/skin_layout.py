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
    'Display': (['NORMAL', 'LIGHT', 'SILENT'], 200),   # TEST tab (temporary)
}
TEST_SWITCH_Y = 120
BOTTOM = [('OUTPUT', 'knob', 'Output'), ('MAX BANDS', 'knob', 'Max Bands'),
          ('ANALYZER', 'switch', 'Analyze'), ('SPEED', 'switch', 'Speed')]

# ---- BANDS tabs: 4 bands x 6 controls -----------------------------------------------------------
BAND_FIELDS = ['On', 'Type', 'Freq', 'Gain', 'Q', 'Slope']
BAND_COL_X = [32 + i * 204 for i in range(6)]
BAND_COL_W = 196
BAND_ROW_Y = [12 + r * 152 for r in range(4)]
BAND_ROW_H = 107
