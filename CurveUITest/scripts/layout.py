"""Shared layout for the Curve UI Test skin (used by make_art.py and gen_skin.py).

Rectangles are (x, y, w, h) in tab coordinates: a tab's content area is 1280 x 628, starting 110 px
down the MPC's 1280 x 800 screen.
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, '..', 'assets', 'fonts', 'TitilliumWeb-Bold.ttf')
TAB_W, TAB_H = 1280, 628

BG = (24, 22, 30)
PANEL = (36, 34, 46)
GRID = (58, 56, 72)
GRID_HI = (96, 92, 120)
TEXT = (220, 220, 232)
TEXT_DIM = (140, 138, 160)
ACCENT = (120, 200, 255)

# Pucks: one colour and letter per pad.
PADS = {'A': (240, 240, 250), 'B': (255, 110, 110), 'C': (80, 210, 200), 'D': (255, 170, 70), 'E': (175, 125, 255)}
PUCK = 48

# Readouts: a caption (drawn into the background) over a Value label.
READOUT_W = 168
READOUT_X0 = 40
CAPTION_H = 20
VALUE_H = 32


def readout_rects(row, i):
    """(caption rect, value rect) of readout i in row 0 or 1."""
    x = READOUT_X0 + i * READOUT_W
    y = 4 + row * 62
    return (x, y, READOUT_W - 8, CAPTION_H), (x, y + CAPTION_H, READOUT_W - 8, VALUE_H)


PAD_TOP = 135
FULL_PAD = (40, PAD_TOP, 1200, 460)
SPLIT_PADS = {"B": (40, PAD_TOP, 580, 460), "C": (660, PAD_TOP, 580, 460)}
STACK_PAD = (40, PAD_TOP, 1200, 460)   # with the puck overhang: bottom at 619 of 628

BUTTON_W, BUTTON_H = 160, 44
BUTTONS = {'reset': (900, 12), 'automove': (1080, 12)}

# What each tab shows. Readouts: (caption, parameter name), row by row.
TABS = [
    ('FULL', [[('UPDATES/S', 'Updates/s'), ('LAST PAD', 'Last Pad'), ('GAP MS', 'Gap ms'), ('PAD A X', 'Pad A X'), ('PAD A Y', 'Pad A Y')],
              [('X MIN', 'X Min'), ('X MAX', 'X Max'), ('Y MIN', 'Y Min'), ('Y MAX', 'Y Max')]]),
    ('SPLIT', [[('UPDATES/S', 'Updates/s'), ('LAST PAD', 'Last Pad'), ('GAP MS', 'Gap ms'), ('PAD B X', 'Pad B X'), ('PAD B Y', 'Pad B Y')],
               [('PAD C X', 'Pad C X'), ('PAD C Y', 'Pad C Y')]]),
    ('STACK', [[('UPDATES/S', 'Updates/s'), ('LAST PAD', 'Last Pad'), ('GAP MS', 'Gap ms'), ('PAD D X', 'Pad D X'), ('PAD D Y', 'Pad D Y')],
               [('PAD E X', 'Pad E X'), ('PAD E Y', 'Pad E Y')]]),
    ('SAFE', []),
    ('CURVE', [[(c, n) for c, n in [('FREQ', 'Bell X'), ('GAIN', 'Bell Y'), ('Q', 'Bell Q'), ('CURVE UPD/S', 'Curve Rate')]]]),
    ('LINES', []),
    ('ENV', []),
    ('ENV2', []),
]
SAFE_SLIDERS = [('Level', (40, 60, 278, 107)), ('Slider 1', (360, 60, 278, 107)), ('Slider 2', (680, 60, 278, 107))]

# ---- CURVE tab -------------------------------------------------------------------------------
GRAPH = (32, 70, 1216, 432)           # 20 Hz..20 kHz (log) x +-18 dB, the same size as EQ Artist's graph
CURVE_COLS, SHADE_COLS = 64, 32
CURVE_FRAMES, SHADE_FRAMES = 73, 37   # 0.5 dB and 1 dB steps
RANGE_DB = 18.0
MARGIN = 12                           # the curve's top / bottom inset
BELL = (255, 110, 110)                # band 1 colour
CURVE = (236, 236, 250)
NODE = 40
CURVE_READOUTS = [('FREQ', 'Bell X'), ('GAIN', 'Bell Y'), ('Q', 'Bell Q'), ('CURVE UPD/S', 'Curve Rate')]
BOTTOM_CAPTION_Y = 510
Q_KNOB = (32, 532, 90, 90)
Q_VALUE = (130, 556, 120, 40)
STRIP_SEGS = (['HALVES', 'TALL', 'LINE'], 300, 86)  # labels, x, segment width
REFRESH_SEGS = (['8', '16', '24', '32', 'ALL'], 600, 70)
SEG_Y, SEG_H = 552, 44
SWEEP_BUTTON = (1080, 552, 168, 44)


# LINE mode: a connected polyline over LINE_COLS columns (8 px), vertices snapped to LINE_STEP_DB. Per
# column, frame = left level x rise, the rise (in LINE_STEP_DB steps) from LINE_RISES (up to 10 dB per
# column); must match kLineCols / kRises in src/CurveUITest.cpp. 289 x 31 = 8,959 frames: each column is
# cut into LINE_TILES stacked tiles of 72 px (images 645,048 px tall, about 124 MB decoded in all).
LINE_COLS = 152
LINE_STEP_DB = 0.125
LINE_LEVELS = 289
LINE_RISES = [-80, -64, -48, -40, -32, -24, -20, -16, -12, -8, -6, -4, -3, -2, -1, 0,
              1, 2, 3, 4, 6, 8, 12, 16, 20, 24, 32, 40, 48, 64, 80]
LINE_TILES = 6


def db_y(db):
    gx, gy, gw, gh = GRAPH
    return gy + MARGIN + (gh - 2 * MARGIN) * (0.5 - db / (2 * RANGE_DB))


# ---- LINES tab -------------------------------------------------------------------------------
PATH_BOXES = [(32, 40, 600, 130), (32, 210, 600, 130), (32, 380, 600, 130)]
PATH_CAPTIONS = ['PATH: STATIC FLAT', 'PATH: STATIC CUBIC BELL', 'PATH: CUBIC BELL, BOUND TO LINE VALUE']
PLOTTER_BOX = (680, 40, 568, 300)
LINE_VALUE_SLIDER = (680, 400, 278, 107)
LINE_VALUE_LABEL = (980, 380, 160, 36)
LINE_SWEEP_BUTTON = (980, 440, 168, 44)


# ---- ENV tab (Experiment B): native Envelope widgets -----------------------------------------
ENV_BOXES = {'ADSR': (32, 40, 600, 250), 'Pitch': (648, 40, 600, 250), 'AD': (32, 340, 600, 250)}
ENV_CAPTIONS = {'ADSR': 'ENVELOPE ADSR (WITH HEADER), SHAPES BOUND', 'Pitch': 'ENVELOPE PITCH (9 POINTS)',
                'AD': 'ENVELOPE AD (NO HEADER, NO FILL)'}
ENV_SWEEP_BUTTON = (680, 360, 168, 44)
ENV_READOUTS = [('ENV A', 'Env A'), ('ENV S', 'Env S'), ('PITCH L2', 'Pitch L2'), ('AD END', 'AD End')]


# ---- ENV2 tab: Pitch envelope geometry (ruler) and curve bending --------------------------------
RULER_ENV = (32, 40, 1216, 200)
RULER_TICKS_Y = 244
BEND_ENVS = {'Pitch': (32, 300, 600, 170), 'ADSR': (648, 300, 600, 170)}
ENV2_SLIDERS = [('Width', (32, 505, 278, 107)), ('Bend', (344, 505, 278, 107))]
