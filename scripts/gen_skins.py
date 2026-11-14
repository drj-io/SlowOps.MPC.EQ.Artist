#!/usr/bin/env python3
"""Write plugin/skins/TUI.json and Q-Links.json for EQ Artist.

Standard library only. Hand-built skin; build.py copies plugin/skins/ verbatim. Parameter
indices come from plugin/build/params.json (written by `build.py --target native`), looked up
by name, so the skin always matches the binary. Layout and palette come from skin_layout.py,
the PNGs from make_skin_art.py.

EQ tab (back to front):
  * the background (graph grid), then 32 spectrum bars (film strips bound to "Spec n");
  * the EQ you hear: 32 bright dots bound to "Resp n";
  * your drawing: 32 small rings bound to "Draw n", over the dots (a dot inside a ring = the EQ
    is where you drew it);
  * the pen: one transparent XYPad over the graph (Pen X / Pen Y), exactly as in the hardware
    spike. The plugin paints the columns from it and refits the bands as you draw;
  * CLEAR / UNDO / BYPASS: image buttons with the label drawn in (make_text_art.py);
  * the fit status ("Fit"'s value text);
  * the bottom row: Output and Max Bands knobs (the SDK kit knob, value label beside it), and
    PRE | POST and SLOW | MED | FAST switches (radio groups of image buttons). Their captions are
    drawn into bg.png.
BANDS 1-4 / BANDS 5-8: one row per band: On, Type, Freq, Gain, Q, Slope.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skin_layout as L  # noqa: E402

PARAMS = os.path.join(L.ROOT, 'plugin', 'build', 'params.json')
OUT = os.path.join(L.ROOT, 'plugin', 'skins')

params = json.load(open(PARAMS))['params']
IDX = {p['name']: p['index'] for p in params}


def P(name):
    return 'Parameter %d' % IDX[name]


def rect(r):
    return ' '.join('%g' % round(v, 2) for v in r)


def entry(name, typ, data, r, show='Show', focus='No', only=None):
    b = {'version': 2 if only else 1, 'acceptsHWFocus': focus, 'showWhenDataModelInvalid': show,
         'whenVisible': 'Always', 'boundsType': 'Absolute', 'bounds': rect(r)}
    if only:
        b['additionalInvalidatingHandles'] = [only]
    return {'version': 2, 'componentData': {'version': 1, 'name': name, 'type': typ, 'data': data},
            'handle remapping': {'version': 1, 'map': []}, 'bounds': b}


def filmstrip(name, img, frames, handle, r):
    return entry(name, 'Image', {'version': 2, 'imageType': 'FilmStrip', 'colour': '0', 'image': img,
                                 'numFrames': frames, 'handleName': handle}, r)


def text_style(size, colour, just):
    return {'version': 1, 'font': {'version': 1, 'name': 'Nexa', 'style': 'Bold', 'height': float(size)},
            'colour': L.argb(colour), 'justification': just, 'case': 'Original'}


def value(name, handle, r, size=22, colour=L.TEXT, just='right verticallyCentred'):
    return entry(name, 'Label', {'version': 1, 'textStyle': text_style(size, colour, just), 'type': 'Value',
                                 'handleName': handle}, r, 'Hide')


def button(name, handle, r):
    return entry(name, 'Button', {'version': 2, 'onImage': name + '_on.png', 'offImage': name + '_off.png',
                                  'buttonId': 1, 'numButtonsInGroup': 1, 'handleName': handle,
                                  'gestureBehaviour': 'Instant'}, r, 'Hide')


def knob(name, handle, r):
    return entry(name, 'Knob', {'version': 3, 'knobType': 'FilmStrip', 'filmStrip': 'knob_uni.png', 'numFrames': 127,
                                'invert': False, 'dragOrientation': 'Vertical', 'handleName': handle}, r, 'Hide', 'Yes')


def segment(name, img, handle, button_id, n, r):
    return entry(name, 'Button', {'version': 2, 'onImage': img + '_on.png', 'offImage': img + '_off.png',
                                  'buttonId': button_id, 'numButtonsInGroup': n, 'handleName': handle,
                                  'gestureBehaviour': 'Instant'}, r, 'Hide')


def slider(name, handle, r):
    return entry(name, 'Slider', {'version': 1, 'handleName': handle}, r, 'Hide', 'Yes')


def xypad(name, x_handle, y_handle, img, r):
    # Exactly the entry the hardware spike used (version 1 bounds, hardware focus on).
    return entry(name, 'XYPad', {'version': 1, 'xPositionHandle': x_handle, 'yPositionHandle': y_handle,
                                 'image': img}, r, 'Show', 'Yes')


def tab(name, key, children):
    bg = {'version': 1, 'colour': L.argb(L.BG), 'image': 'bg.png' if key == 0 else ''}
    return {'version': 1, 'tabName': name, 'fnKeyIndex': key, 'fnKeySubIndex': 0,
            'componentDefinition': {'version': 2, 'actions': [],
                                    'backgroundData': {'version': 1, 'focussed': bg, 'unfocussed': bg},
                                    'ignoreMousePresses': False, 'disableCoarseDataWheel': False,
                                    'componentsData': children},
            'qlinkBoundsData': []}


def col_rect(c):
    gx, gy, _, gh = L.GRAPH
    return (gx + c * L.COL_W, gy, L.COL_W, gh)


def eq_tab():
    out = []
    out += [filmstrip('spec %d' % (c + 1), 'spec_bar.png', L.SPEC_FRAMES, P('Spec %d' % (c + 1)), col_rect(c))
            for c in range(L.NUM_COLS)]
    out += [filmstrip('resp %d' % (c + 1), 'dot.png', L.DOT_FRAMES, P('Resp %d' % (c + 1)), col_rect(c))
            for c in range(L.NUM_COLS)]
    out += [filmstrip('draw %d' % (c + 1), 'draw_dot.png', L.DOT_FRAMES, P('Draw %d' % (c + 1)), col_rect(c))
            for c in range(L.NUM_COLS)]

    gx, gy, gw, gh = L.GRAPH   # the axis labels are drawn into bg.png (make_text_art.py)

    # The pen. The puck stays inside its pad, so the pad overhangs the graph by half a puck on
    # every side: the puck's centre then reaches the graph's edges.
    h = L.PUCK / 2.0
    out.append(xypad('pen', P('Pen X'), P('Pen Y'), 'puck.png', (gx - h, gy - h, gw + 2 * h, gh + 2 * h)))

    for (pname, caption), x in zip(L.BUTTONS, L.BUTTON_X):
        out.append(button(caption.lower(), P(pname), (x, L.BUTTON_Y, L.BUTTON_W, L.BUTTON_H)))
    out.append(value('fit info', P('Fit'), L.FIT_INFO))
    for (caption, kind, pname), x in zip(L.BOTTOM, L.BLOCK_X):
        if kind == 'knob':
            out.append(knob(caption.lower(), P(pname), (x, L.KNOB_Y, L.KNOB, L.KNOB)))
            vx, vy, vw, vh = L.KNOB_VALUE
            out.append(value(caption.lower() + ' value', P(pname), (x + vx, vy, vw, vh), 28, L.TEXT, 'left verticallyCentred'))
        else:
            labels, w = L.SWITCHES[pname]
            for i, lab in enumerate(labels):
                out.append(segment('%s %s' % (caption.lower(), lab.lower()), 'seg_' + lab.lower(), P(pname), i, len(labels),
                                   (x + i * (w + 6), L.SEG_Y, w, L.SEG_H)))
    return out


def bands_tab(first):
    out = []
    for r in range(4):
        b = first + r + 1
        for c, field in enumerate(L.BAND_FIELDS):
            out.append(slider('b%d %s' % (b, field.lower()), P('B%d %s' % (b, field)),
                              (L.BAND_COL_X[c], L.BAND_ROW_Y[r], L.BAND_COL_W, L.BAND_ROW_H)))
    return out


def main():
    types = ['Bell', 'Low Shelf', 'High Shelf', 'Low Cut', 'High Cut', 'Notch', 'Band Pass', 'Tilt']
    slopes = ['6 dB', '12 dB', '18 dB', '24 dB', '36 dB', '48 dB']
    reps = [{'key': P(n), 'value': ['Off', 'On']} for n in ('Clear', 'Undo', 'Bypass', 'Analyzer')]
    reps += [{'key': P('Analyze'), 'value': ['Pre', 'Post']},
             {'key': P('Speed'), 'value': ['Slow', 'Medium', 'Fast']},
             {'key': P('Max Bands'), 'value': [str(k) for k in range(1, 9)]}]
    for b in range(1, 9):
        reps += [{'key': P('B%d On' % b), 'value': ['Off', 'On']},
                 {'key': P('B%d Type' % b), 'value': types},
                 {'key': P('B%d Slope' % b), 'value': slopes}]

    eq, b14, b58 = eq_tab(), bands_tab(0), bands_tab(4)
    tui = {'pageData': {
        'version': 1,
        'componentDefinitions': {'version': 2, 'importFiles': [], 'localComponentDefinitions': []},
        'replacementStrings': reps,
        'info': {'version': 1, 'type': 'CompleteDescription'},
        'tabs': [tab('EQ', 0, eq), tab('BANDS 1-4', 1, b14), tab('BANDS 5-8', 2, b58)]}}

    # Q-Links (4 x 4, Q-Link 13 top left). EQ tab and Program mode: Freq / Gain of all 8 bands,
    # two bands per row. BANDS tabs: one band per row: Freq, Gain, Q, Type.
    def row_base(r):
        return 13 - 4 * r

    eq_q = {}
    for b in range(8):
        base = row_base(b // 2) + 2 * (b % 2)
        eq_q['Q-Link %d' % base] = IDX['B%d Freq' % (b + 1)]
        eq_q['Q-Link %d' % (base + 1)] = IDX['B%d Gain' % (b + 1)]

    def bands_q(first):
        q = {}
        for r in range(4):
            for k, field in enumerate(('Freq', 'Gain', 'Q', 'Type')):
                q['Q-Link %d' % (row_base(r) + k)] = IDX['B%d %s' % (first + r + 1, field)]
        return q

    def full(m):
        return {'Q-Link %d' % k: m.get('Q-Link %d' % k, -1) for k in range(1, 17)}

    prog = {}
    for b in range(8):
        prog['Q-Link %d' % (2 * b + 1)] = IDX['B%d Freq' % (b + 1)]
        prog['Q-Link %d' % (2 * b + 2)] = IDX['B%d Gain' % (b + 1)]
    qlinks = {'version': 4, 'info': {'version': 1, 'type': 'CompleteDescription'},
              'Screen Mode Q-Links': {'version': 4, 'map': [
                  {'Tab': 1, 'SubTab': 1, 'Bank Direction': 'Column', 'Q-Links': full(eq_q)},
                  {'Tab': 2, 'SubTab': 1, 'Bank Direction': 'Column', 'Q-Links': full(bands_q(0))},
                  {'Tab': 3, 'SubTab': 1, 'Bank Direction': 'Column', 'Q-Links': full(bands_q(4))}]},
              'Program Mode Q-Links': full(prog)}

    os.makedirs(OUT, exist_ok=True)
    json.dump(tui, open(os.path.join(OUT, 'TUI.json'), 'w'), indent=2)
    json.dump(qlinks, open(os.path.join(OUT, 'Q-Links.json'), 'w'), indent=2)
    print('skins: TUI.json (EQ %d, BANDS 1-4 %d, BANDS 5-8 %d components), Q-Links.json' % (len(eq), len(b14), len(b58)))


if __name__ == '__main__':
    main()
