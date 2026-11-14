#!/usr/bin/env python3
"""Write plugin/skins/TUI.json and Q-Links.json for EQ Artist (SPIKE build: three test tabs).

Standard library only. Hand-built skin; build.py copies plugin/skins/ verbatim. Parameter
indices come from plugin/build/params.json (written by `build.py --target native`), looked up
by name, so the skin always matches the binary. Layout and palette come from skin_layout.py,
the PNGs from make_skin_art.py.

Tabs (see docs/SPIKE.md for what to check on the device):
  PEN      spectrum bars, the drawn curve as film-strip dots, and one transparent XYPad over the
           whole graph bound to Pen X / Pen Y. The plugin paints the columns from the pen.
  COLUMNS  spectrum bars and 32 vertical sliders (Draw 1..32) with a dot thumb. Swipe across
           them: "Swipe cols" says how many columns one gesture changed.
  OVERLAP  the sliders with the pen XYPad on top, to see which widget takes the touch.
Every tab has the same bottom row: Clear, Source (Input / Sweep), Disp Rate, Flip Y.
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


NO_BG = {'version': 1, 'focussed': {'version': 1, 'colour': '0', 'image': ''},
         'unfocussed': {'version': 1, 'colour': '0', 'image': ''}}


def rect(r):
    return ' '.join('%g' % round(v, 2) for v in r)


def entry(name, typ, data, r, show='Show', focus='No'):
    return {'version': 2, 'componentData': {'version': 1, 'name': name, 'type': typ, 'data': data},
            'handle remapping': {'version': 1, 'map': []},
            'bounds': {'version': 1, 'acceptsHWFocus': focus, 'showWhenDataModelInvalid': show,
                       'whenVisible': 'Always', 'boundsType': 'Absolute', 'bounds': rect(r)}}


def filmstrip(name, img, frames, handle, r):
    return entry(name, 'Image', {'version': 2, 'imageType': 'FilmStrip', 'colour': '0', 'image': img,
                                 'numFrames': frames, 'handleName': handle}, r)


def text_style(size, colour, just):
    return {'version': 1, 'font': {'version': 1, 'name': 'Nexa', 'style': 'Bold', 'height': float(size)},
            'colour': L.argb(colour), 'justification': just, 'case': 'Original'}


def text(name, s, r, size=16, colour=L.TEXT_DIM, just='horizontallyCentred verticallyCentred'):
    return entry(name, 'Label', {'version': 1, 'textStyle': text_style(size, colour, just), 'type': 'Text',
                                 'handleName': 'Invalid', 'text': s}, r)


def value(name, handle, r, size=24, colour=L.TEXT, just='left verticallyCentred'):
    return entry(name, 'Label', {'version': 1, 'textStyle': text_style(size, colour, just), 'type': 'Value',
                                 'handleName': handle}, r, 'Hide')


def vslider(name, handle, r):
    return entry(name, 'Slider', {'version': 4, 'sliderType': 'TrackAndThumb', 'direction': 'Vertical',
                                  'thumbImage': 'thumb.png', 'thumbTrackVisible': False, 'revealedImage': '',
                                  'revealType': 'Normal', 'handleName': handle}, r, 'Hide', 'Yes')


def generic_slider(name, handle, r):
    return entry(name, 'Slider', {'version': 1, 'handleName': handle}, r, 'Hide', 'Yes')


def xypad(name, x_handle, y_handle, img, r):
    return entry(name, 'XYPad', {'version': 1, 'xPositionHandle': x_handle, 'yPositionHandle': y_handle,
                                 'image': img}, r, 'Show', 'Yes')


def tab(name, key, children):
    bg = {'version': 1, 'colour': L.argb(L.BG), 'image': 'bg.png'}
    return {'version': 1, 'tabName': name, 'fnKeyIndex': key, 'fnKeySubIndex': 0,
            'componentDefinition': {'version': 2, 'actions': [],
                                    'backgroundData': {'version': 1, 'focussed': bg, 'unfocussed': bg},
                                    'ignoreMousePresses': False, 'disableCoarseDataWheel': False,
                                    'componentsData': children},
            'qlinkBoundsData': []}


def col_rect(c):
    gx, gy, _, gh = L.GRAPH
    return (gx + c * L.COL_W, gy, L.COL_W, gh)


def spectrum():
    return [filmstrip('spec %d' % (c + 1), 'spec_bar.png', L.SPEC_FRAMES, P('Spec %d' % (c + 1)), col_rect(c))
            for c in range(L.NUM_COLS)]


def curve_dots():
    return [filmstrip('dot %d' % (c + 1), 'dot.png', L.DOT_FRAMES, P('Draw %d' % (c + 1)), col_rect(c))
            for c in range(L.NUM_COLS)]


def sliders():
    return [vslider('draw %d' % (c + 1), P('Draw %d' % (c + 1)), col_rect(c)) for c in range(L.NUM_COLS)]


def pen():
    # The puck stays inside its pad, so the pad overhangs the graph by half a puck on every side:
    # the puck's centre then reaches the graph's edges.
    gx, gy, gw, gh = L.GRAPH
    h = L.PUCK / 2.0
    return [xypad('pen', P('Pen X'), P('Pen Y'), 'puck.png', (gx - h, gy - h, gw + 2 * h, gh + 2 * h))]


def axis_labels():
    gx, gy, gw, gh = L.GRAPH
    out = [text('f %s' % s, s, (L.freq_x(f) - 30, gy + gh - 26, 60, 22)) for f, s in L.FREQ_LABELS]
    for db in (12, 0, -12):
        y = L.db_y(db)
        out.append(text('db %d' % db, '%+d' % db if db else '0', (gx + 6, y - 22, 50, 20), 15,
                        just='left verticallyCentred'))
    return out


def readouts(items):
    """items: list of (caption, param name). Laid out left to right along the top strip."""
    out, x = [], L.GRAPH[0]
    for cap, name in items:
        out.append(text('cap ' + cap, cap, (x, L.READOUT_Y, 130, L.READOUT_H), 18, L.TEXT_DIM, 'left verticallyCentred'))
        out.append(value('val ' + cap, P(name), (x + 132, L.READOUT_Y, 110, L.READOUT_H)))
        x += 260
    return out


def controls():
    names = ('Clear', 'Source', 'Disp Rate', 'Flip Y')
    return [generic_slider(n.lower(), P(n), (x, L.CONTROL_Y, L.CONTROL_W, L.CONTROL_H))
            for n, x in zip(names, L.CONTROL_X)]


def main():
    pen_tab = ([] + spectrum() + curve_dots() + axis_labels() + pen()
               + readouts([('PEN /S', 'Pen Rate'), ('POINTS', 'Pen Pts'), ('X', 'Pen X'), ('Y', 'Pen Y')]) + controls())
    cols_tab = (spectrum() + axis_labels() + sliders()
                + readouts([('SWIPE COLS', 'Swipe Cols')]) + controls())
    overlap_tab = (spectrum() + axis_labels() + sliders() + pen()
                   + readouts([('PEN /S', 'Pen Rate'), ('SWIPE COLS', 'Swipe Cols')]) + controls())

    reps = [{'key': P('Clear'), 'value': ['Off', 'On']},
            {'key': P('Flip Y'), 'value': ['Off', 'On']},
            {'key': P('Source'), 'value': ['Input', 'Sweep']},
            {'key': P('Disp Rate'), 'value': ['30 Hz', '15 Hz', '60 Hz']}]

    tui = {'pageData': {
        'version': 1,
        'componentDefinitions': {'version': 2, 'importFiles': [], 'localComponentDefinitions': []},
        'replacementStrings': reps,
        'info': {'version': 1, 'type': 'CompleteDescription'},
        'tabs': [tab('PEN', 0, pen_tab), tab('COLUMNS', 1, cols_tab), tab('OVERLAP', 2, overlap_tab)]}}

    # Only Output on a knob (Q-Link 13, top left); the triggers and drawing never go on a knob.
    q = {'Q-Link %d' % k: -1 for k in range(1, 17)}
    q['Q-Link 13'] = IDX['Output']
    prog = {'Q-Link %d' % k: -1 for k in range(1, 17)}
    prog['Q-Link 1'] = IDX['Output']
    qlinks = {'version': 4, 'info': {'version': 1, 'type': 'CompleteDescription'},
              'Screen Mode Q-Links': {'version': 4, 'map': [
                  {'Tab': t, 'SubTab': 1, 'Bank Direction': 'Column', 'Q-Links': dict(q)} for t in (1, 2, 3)]},
              'Program Mode Q-Links': prog}

    os.makedirs(OUT, exist_ok=True)
    json.dump(tui, open(os.path.join(OUT, 'TUI.json'), 'w'), indent=2)
    json.dump(qlinks, open(os.path.join(OUT, 'Q-Links.json'), 'w'), indent=2)
    print('skins: TUI.json (PEN %d, COLUMNS %d, OVERLAP %d components), Q-Links.json'
          % (len(pen_tab), len(cols_tab), len(overlap_tab)))


if __name__ == '__main__':
    main()
