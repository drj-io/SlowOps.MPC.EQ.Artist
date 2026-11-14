#!/usr/bin/env python3
"""Write skins/TUI.json and Q-Links.json for Curve UI Test (standard library only).

Parameter indices come from build/params.json (written by the SDK's native build), looked up by name.

Every XYPad's bounds overhang its drawn panel by half a puck on each side: the puck stays inside its pad,
so this lets the puck's centre reach the panel's edges (as on EQ Artist's DRAW page).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout as L  # noqa: E402

params = json.load(open(os.path.join(L.ROOT, 'build', 'params.json')))['params']
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


def filmstrip(name, img, frames, handle, r, only=None):
    return entry(name, 'Image', {'version': 2, 'imageType': 'FilmStrip', 'colour': '0', 'image': img,
                                 'numFrames': frames, 'handleName': handle}, r, only=only)


def segment(name, img, handle, button_id, n, r):
    return entry(name, 'Button', {'version': 2, 'onImage': img + '_on.png', 'offImage': img + '_off.png',
                                  'buttonId': button_id, 'numButtonsInGroup': n, 'handleName': handle,
                                  'gestureBehaviour': 'Instant'}, r, 'Hide')


def toggle(name, img, handle, r):
    return segment(name, img, handle, 1, 1, r)


def argb(rgb, a=255):
    return '%02x%02x%02x%02x' % (a, rgb[0], rgb[1], rgb[2])


def curve_tab():
    gx, gy, gw, gh = L.GRAPH
    out = []
    sw = gw // L.SHADE_COLS
    for c in range(L.SHADE_COLS):   # band shading, under the curve
        out.append(filmstrip('shade %d' % (c + 1), 'shade.png', L.SHADE_FRAMES, P('Shade %d' % (c + 1)), (gx + c * sw, gy, sw, gh)))
    w = gw // L.CURVE_COLS
    halves = 'IndexedEnabling/0/3/%s' % P('Strip')
    tall = 'IndexedEnabling/1/3/%s' % P('Strip')
    line = 'IndexedEnabling/2/3/%s' % P('Strip')
    th = gh // L.LINE_TILES
    nline = L.LINE_LEVELS * len(L.LINE_RISES)
    for c in range(L.CURVE_COLS):
        h = P('Curve %d' % (c + 1))
        x = gx + c * w
        out.append(filmstrip('curve %d upper' % (c + 1), 'curve_upper.png', L.CURVE_FRAMES, h, (x, gy, w, gh // 2), halves))
        out.append(filmstrip('curve %d lower' % (c + 1), 'curve_lower.png', L.CURVE_FRAMES, h, (x, gy + gh // 2, w, gh // 2), halves))
        out.append(filmstrip('curve %d tall' % (c + 1), 'curve_tall.png', L.CURVE_FRAMES, h, (x, gy, w, gh), tall))

    lw = gw // L.LINE_COLS
    for c in range(L.LINE_COLS):
        for t in range(L.LINE_TILES):
            out.append(filmstrip('line %d t%d' % (c + 1, t), 'line_t%d.png' % t, nline, P('Line %d' % (c + 1)),
                                 (gx + c * lw, gy + t * th, lw, th), line))
    o = L.NODE / 2.0
    out.append(entry('bell node', 'XYPad', {'version': 1, 'xPositionHandle': P('Bell X'), 'yPositionHandle': P('Bell Y'),
                                            'image': 'node_1.png'},
                     # Vertically the curve is inset by MARGIN (db_y), so the pad is too: Y = 0 / 1 = -18 / +18 dB lines.
                     (gx - o, gy + L.MARGIN - o, gw + 2 * o, gh - 2 * L.MARGIN + 2 * o), 'Show', 'Yes'))
    out += readouts([L.CURVE_READOUTS])
    out.append(entry('q', 'Knob', {'version': 3, 'knobType': 'FilmStrip', 'filmStrip': 'knob_uni.png', 'numFrames': 127,
                                   'invert': False, 'dragOrientation': 'Vertical', 'handleName': P('Bell Q')}, L.Q_KNOB, 'Hide', 'Yes'))
    out.append(value('q value', P('Bell Q'), L.Q_VALUE))
    for (labels, x0, sw2), pname in ((L.STRIP_SEGS, 'Strip'), (L.REFRESH_SEGS, 'Refresh')):
        for i, lab in enumerate(labels):
            out.append(segment('%s %s' % (pname.lower(), lab.lower()), 'seg_' + lab.lower(), P(pname), i, len(labels),
                               (x0 + i * (sw2 + 6), L.SEG_Y, sw2, L.SEG_H)))
    out.append(toggle('sweep', 'sweep', P('Sweep'), L.SWEEP_BUTTON))
    return out


def lines_tab():
    out = []
    w, h = L.PATH_BOXES[0][2], L.PATH_BOXES[0][3]
    flat = 'm 0 %g l %g %g' % (h / 2, w, h / 2)
    bell = 'm 0 %g c %g %g %g %g %g %g c %g %g %g %g %g %g' % (
        h - 20, w * 0.25, h - 20, w * 0.37, 15, w * 0.5, 15, w * 0.63, 15, w * 0.75, h - 20, w, h - 20)
    for i, (path, handle) in enumerate(((flat, 'Invalid'), (bell, 'Invalid'), (bell, P('Line Value')))):
        out.append(entry('path %d' % (i + 1), 'Path', {'version': 1, 'handleName': handle, 'path': path, 'drawPath': True,
                                                       'colour': argb(L.ACCENT), 'opacity': 1.0}, L.PATH_BOXES[i]))
    out.append(entry('plotter', 'Plotter', {
        'version': 2, 'backgroundColour': '0', 'drawLine': True,
        'lineColourStart': argb(L.ACCENT), 'lineColourEnd': argb(L.BELL), 'horizLineGradient': True, 'lineWidth': 3.0,
        'fillColourStart': argb(L.ACCENT, 0x50), 'fillColourEnd': argb(L.ACCENT, 0x00), 'horizFillGradient': False,
        'fillZero': 0.0, 'dataHandle': P('Line Value'), 'dataMin': 0.0, 'dataMax': 1.0,
        'drawHistory': True, 'historyDirection': 'Left',
        'historyColour1': argb(L.ACCENT), 'historyColour2': argb(L.BELL, 0x60),
        'historyTime': 4.0, 'historySkipFrames': 1}, L.PLOTTER_BOX))
    out.append(entry('line value', 'Slider', {'version': 1, 'handleName': P('Line Value')}, L.LINE_VALUE_SLIDER, 'Hide', 'Yes'))
    out.append(value('line value text', P('Line Value'), L.LINE_VALUE_LABEL))
    out.append(toggle('line sweep', 'linesweep', P('Line Sweep'), L.LINE_SWEEP_BUTTON))
    return out


def value(name, handle, r):
    style = {'version': 1, 'font': {'version': 1, 'name': 'Nexa', 'style': 'Bold', 'height': 26.0},
             'colour': 'ff%02x%02x%02x' % L.TEXT, 'justification': 'left verticallyCentred', 'case': 'Original'}
    return entry(name, 'Label', {'version': 1, 'textStyle': style, 'type': 'Value', 'handleName': handle}, r, 'Hide')


def xypad(letter, panel):
    x, y, w, h = panel
    o = L.PUCK / 2.0
    return entry('pad ' + letter, 'XYPad', {'version': 1, 'xPositionHandle': P('Pad %s X' % letter),
                                            'yPositionHandle': P('Pad %s Y' % letter),
                                            'image': 'puck_%s.png' % letter.lower()},
                 (x - o, y - o, w + 2 * o, h + 2 * o), 'Show', 'Yes')


def button(name, pname):
    x, y = L.BUTTONS[name]
    return entry(name, 'Button', {'version': 2, 'onImage': name + '_on.png', 'offImage': name + '_off.png',
                                  'buttonId': 1, 'numButtonsInGroup': 1, 'handleName': P(pname),
                                  'gestureBehaviour': 'Instant'}, (x, y, L.BUTTON_W, L.BUTTON_H), 'Hide')


def readouts(rows):
    out = []
    for row, items in enumerate(rows):
        for i, (cap, pname) in enumerate(items):
            _, vr = L.readout_rects(row, i)
            out.append(value(cap.lower(), P(pname), vr))
    return out


def tab(name, key, children):
    bg = {'version': 1, 'colour': 'ff%02x%02x%02x' % L.BG, 'image': 'bg_%s.png' % name.lower()}
    return {'version': 1, 'tabName': name, 'fnKeyIndex': key, 'fnKeySubIndex': 0,
            'componentDefinition': {'version': 2, 'actions': [],
                                    'backgroundData': {'version': 1, 'focussed': bg, 'unfocussed': bg},
                                    'ignoreMousePresses': False, 'disableCoarseDataWheel': False,
                                    'componentsData': children},
            'qlinkBoundsData': []}


def envelope(name, env_type, header, stage_handles, shape_handles, r, fill=True, bare=False, curve='Quadratic',
             hide_shapes=True):
    """An Envelope entry with the keys in the reference's order (docs/ui-reference.md 6.13). bare = only the
    line: transparent background / scale / fill, zero-size invisible handles."""
    blue = 'ff5b9cff'
    clear = '00000000'
    d = {'version': 1, 'envelopeType': env_type, 'envelopeStyle': 'WithHeader' if header else 'WithoutHeader',
         'envelopeName': name.upper(), 'backgroundColour': clear if bare else 'ff1e1d24',
         'scaleColour': clear if bare else 'ff3a3a48',
         'lineColour': blue, 'fillColour': '405b9cff' if fill and not bare else clear, 'highlightColour': clear if bare else 'ffffffff',
         'lineWidth': 3, 'handleWidth': 0 if bare else 15, 'handleHeight': 0 if bare else 10, 'handleShape': 'Circle',
         'handleLineWidth': 0 if bare else 2,
         'handleLineColour': clear if bare else 'ffffffff', 'handleFillColour': clear if bare else 'ff1e1d24',
         'handleTextColour': clear if bare else 'ffffffff',
         'handleLineWidthOnPressed': 0 if bare else 2, 'handleLineColourOnPressed': clear if bare else 'ffffffff',
         'handleFillColourOnPressed': clear if bare else blue,
         'handleTextColourOnPressed': clear if bare else 'ffffffff', 'handleArrowLineColourOnPressed': clear if bare else 'ffffffff',
         'handleArrowFillColourOnPressed': clear if bare else blue}
    for key, pname in stage_handles:
        d[key] = P(pname)
    for key, pname in shape_handles:
        d[key] = P(pname) if pname else 'Invalid'
    d['forceHideShapeHandles'] = hide_shapes
    d['forceCurveShape'] = 0.5
    d['curveType'] = curve
    return entry(name, 'Envelope', d, r, 'Show', 'No' if bare else 'Yes')


PITCH_KEYS = ['pitchEnvelopeLevel1Handle', 'pitchEnvelopeDelayHandle', 'pitchEnvelopeLevel2Handle',
              'pitchEnvelopeAttackHandle', 'pitchEnvelopeLevel3Handle', 'pitchEnvelopeDecayHandle',
              'pitchEnvelopeSustainHandle', 'pitchEnvelopeReleaseHandle', 'pitchEnvelopeLevel5Handle']
RULER_NAMES = ['Ruler L1', 'Ruler Delay', 'Ruler L2', 'Ruler Attack', 'Ruler L3', 'Ruler Decay', 'Ruler Sustain',
               'Ruler Release', 'Ruler L5']


def env2_tab():
    bend = [('attackShapeHandle', 'Bend'), ('decayShapeHandle', 'Bend'), ('releaseShapeHandle', 'Bend')]
    out = [
        envelope('ruler', 'Pitch', False, list(zip(PITCH_KEYS, RULER_NAMES)),
                 [('attackShapeHandle', None), ('decayShapeHandle', None), ('releaseShapeHandle', None)],
                 L.RULER_ENV, fill=False, bare=True),
        envelope('pitch bend', 'Pitch', False, list(zip(PITCH_KEYS, RULER_NAMES)), bend, L.BEND_ENVS['Pitch'],
                 curve='Exponential', hide_shapes=False),
        envelope('adsr bend', 'ADSR', False,
                 [('attackHandle', 'Env A'), ('decayHandle', 'Env D'), ('sustainHandle', 'Env S'), ('releaseHandle', 'Env R')],
                 bend, L.BEND_ENVS['ADSR'], curve='Exponential', hide_shapes=False),
    ]
    for n, r in L.ENV2_SLIDERS:
        out.append(entry(n.lower(), 'Slider', {'version': 1, 'handleName': P(n)}, r, 'Hide', 'Yes'))
    return out


def env_tab():
    out = [
        envelope('adsr', 'ADSR', True,
                 [('attackHandle', 'Env A'), ('decayHandle', 'Env D'), ('sustainHandle', 'Env S'), ('releaseHandle', 'Env R')],
                 [('attackShapeHandle', 'Env Shape A'), ('decayShapeHandle', 'Env Shape D'), ('releaseShapeHandle', 'Env Shape R')],
                 L.ENV_BOXES['ADSR']),
        envelope('pitch', 'Pitch', True,
                 [('pitchEnvelopeLevel1Handle', 'Pitch L1'), ('pitchEnvelopeDelayHandle', 'Pitch Delay'),
                  ('pitchEnvelopeLevel2Handle', 'Pitch L2'), ('pitchEnvelopeAttackHandle', 'Pitch Attack'),
                  ('pitchEnvelopeLevel3Handle', 'Pitch L3'), ('pitchEnvelopeDecayHandle', 'Pitch Decay'),
                  ('pitchEnvelopeSustainHandle', 'Pitch Sustain'), ('pitchEnvelopeReleaseHandle', 'Pitch Release'),
                  ('pitchEnvelopeLevel5Handle', 'Pitch L5')],
                 [('attackShapeHandle', None), ('decayShapeHandle', None), ('releaseShapeHandle', None)],
                 L.ENV_BOXES['Pitch']),
        envelope('ad', 'AD', False,
                 [('attackHandle', 'AD Attack'), ('decayHandle', 'AD Decay'), ('decayFromEndHandle', 'AD End')],
                 [('attackShapeHandle', None), ('decayShapeHandle', None)],
                 L.ENV_BOXES['AD'], fill=False),
        toggle('env sweep', 'envsweep', P('Env Sweep'), L.ENV_SWEEP_BUTTON),
    ]
    for i, (cap, pname) in enumerate(L.ENV_READOUTS):
        x, y = 680 + (i % 2) * 280, 460 + (i // 2) * 70
        out.append(value(cap.lower(), P(pname), (x, y, 200, 34)))
    return out


def main():
    t = dict(L.TABS)
    full = readouts(t['FULL']) + [button('reset', 'Reset'), button('automove', 'Auto Move'), xypad('A', L.FULL_PAD)]
    split = readouts(t['SPLIT']) + [xypad('B', L.SPLIT_PADS['B']), xypad('C', L.SPLIT_PADS['C'])]
    stack = readouts(t['STACK']) + [xypad('D', L.STACK_PAD), xypad('E', L.STACK_PAD)]   # E is drawn on top
    safe = [entry(n.lower(), 'Slider', {'version': 1, 'handleName': P(n)}, r, 'Hide', 'Yes') for n, r in L.SAFE_SLIDERS]
    curve, lines, env, env2 = curve_tab(), lines_tab(), env_tab(), env2_tab()
    tabs = [tab('FULL', 0, full), tab('SPLIT', 1, split), tab('STACK', 2, stack), tab('SAFE', 3, safe),
            tab('CURVE', 4, curve), tab('LINES', 5, lines), tab('ENV', 6, env), tab('ENV2', 7, env2)]

    reps = [{'key': P(n), 'value': ['Off', 'On']} for n in ('Auto Move', 'Reset', 'Sweep', 'Line Sweep', 'Env Sweep')]
    reps += [{'key': P('Strip'), 'value': ['Halves', 'Tall', 'Line']}, {'key': P('Refresh'), 'value': ['8', '16', '24', '32', 'All']}]
    tui = {'pageData': {'version': 1,
                        'componentDefinitions': {'version': 2, 'importFiles': [], 'localComponentDefinitions': []},
                        'replacementStrings': reps, 'info': {'version': 1, 'type': 'CompleteDescription'},
                        'tabs': tabs}}

    def qmap(pairs):
        m = {'Q-Link %d' % k: -1 for k in range(1, 17)}
        for k, name in pairs:
            m['Q-Link %d' % k] = IDX[name]
        return m

    screen = [qmap([(13, 'Pad A X'), (14, 'Pad A Y')]),
              qmap([(13, 'Pad B X'), (14, 'Pad B Y'), (15, 'Pad C X'), (16, 'Pad C Y')]),
              qmap([(13, 'Pad D X'), (14, 'Pad D Y'), (15, 'Pad E X'), (16, 'Pad E Y')]),
              qmap([(13, 'Level'), (14, 'Slider 1'), (15, 'Slider 2')]),
              qmap([(13, 'Bell X'), (14, 'Bell Y'), (15, 'Bell Q')]),
              qmap([(13, 'Line Value')]),
              qmap([(13, 'Env A'), (14, 'Env D'), (15, 'Env S'), (16, 'Env R'), (9, 'Env Shape A'), (10, 'Env Shape D'),
                    (11, 'Env Shape R')]),
              qmap([(13, 'Width'), (14, 'Bend')])]
    qlinks = {'version': 4, 'info': {'version': 1, 'type': 'CompleteDescription'},
              'Screen Mode Q-Links': {'version': 4, 'map': [
                  {'Tab': i + 1, 'SubTab': 1, 'Bank Direction': 'Column', 'Q-Links': m} for i, m in enumerate(screen)]},
              'Program Mode Q-Links': qmap([(1, 'Pad A X'), (2, 'Pad A Y')])}
    out = os.path.join(L.ROOT, 'skins')
    json.dump(tui, open(os.path.join(out, 'TUI.json'), 'w'), indent=2)
    json.dump(qlinks, open(os.path.join(out, 'Q-Links.json'), 'w'), indent=2)
    print('skin: %s' % ', '.join('%s %d' % (t['tabName'], len(t['componentDefinition']['componentsData'])) for t in tabs))


if __name__ == '__main__':
    main()
