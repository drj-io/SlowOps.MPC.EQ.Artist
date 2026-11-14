#!/usr/bin/env python3
"""Write plugin/skins/TUI.json and Q-Links.json for EQ Artist.

Standard library only. Hand-built skin; build.py copies plugin/skins/ verbatim. Parameter
indices come from plugin/build/params.json (written by `build.py --target native`), looked up
by name, so the skin always matches the binary. Layout and palette come from skin_layout.py,
the PNGs from make_skin_art.py.

DRAW tab (back to front):
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
PARA (see para_tab()): the interactive parametric page, UI test with silent bands. Q-Links: on DRAW the
Freq / Gain of bands 1-8; on PARA the selected band's Freq, Gain, Q and Slope.
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


def filmstrip(name, img, frames, handle, r, only=None):
    return entry(name, 'Image', {'version': 2, 'imageType': 'FilmStrip', 'colour': '0', 'image': img,
                                 'numFrames': frames, 'handleName': handle}, r, only=only)


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


def segment(name, img, handle, button_id, n, r, only=None):
    return entry(name, 'Button', {'version': 2, 'onImage': img + '_on.png', 'offImage': img + '_off.png',
                                  'buttonId': button_id, 'numButtonsInGroup': n, 'handleName': handle,
                                  'gestureBehaviour': 'Instant'}, r, 'Hide', 'No', only)


def xypad(name, x_handle, y_handle, img, r, only=None, focus='Yes'):
    # The pen is exactly the entry the hardware spike used (version 1 bounds, hardware focus on).
    return entry(name, 'XYPad', {'version': 1, 'xPositionHandle': x_handle, 'yPositionHandle': y_handle,
                                 'image': img}, r, 'Show', focus, only)


def tab(name, key, children, image):
    bg = {'version': 1, 'colour': L.argb(L.BG), 'image': image}
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


def para_tab():
    """PARA (UI test, silent bands). Back to front: spectrum, the selected band's shade (one set per
    colour, only the selected one shown), the connected line (152 columns x 6 tiles), selection rings,
    band nodes (display-only XYPads), and the pen (one transparent XYPad over the graph, hit-tested by
    the plugin: stacked pads cannot be separate nodes on the MPC)."""
    gx, gy, gw, gh = L.GRAPH
    sel = P('Para Sel')
    bands = P('Para Bands')
    out = [filmstrip('spec %d' % (c + 1), 'spec_bar.png', L.SPEC_FRAMES, P('Spec %d' % (c + 1)), col_rect(c))
           for c in range(L.NUM_COLS)]
    nsel = L.PARA_BANDS + 1   # Sel: 0 = none, 1..8 = a band
    # The selected band's flat fill: one strip for every colour (frame = colour x height), 152 columns.
    fw = gw // L.LINE_COLS
    nfill = len(L.PARA_COLOURS) * L.FILL_LEVELS
    out += [filmstrip('fill %d' % (c + 1), 'para_fill.png', nfill, P('PFill %d' % (c + 1)), (gx + c * fw, gy, fw, gh))
            for c in range(L.LINE_COLS)]
    style = P('Para Style')
    as_dots, as_line = 'IndexedEnabling/0/2/%s' % style, 'IndexedEnabling/1/2/%s' % style
    lw, th = gw // L.LINE_COLS, gh // L.LINE_TILES
    nline = L.LINE_LEVELS * len(L.LINE_RISES)
    dw = gw // L.SHADE_COLS

    def line_layer(prefix, pname):
        return [filmstrip('%s %d t%d' % (prefix, c + 1, t), '%s_t%d.png' % (prefix, t), nline, P('%s %d' % (pname, c + 1)),
                          (gx + c * lw, gy + t * th, lw, th), as_line)
                for c in range(L.LINE_COLS) for t in range(L.LINE_TILES)]

    def dot_layer(prefix, pname):
        out2 = []
        for c in range(L.SHADE_COLS):
            h2 = P('%s %d' % (pname, c + 1))
            out2.append(filmstrip('%s %d up' % (prefix, c + 1), '%s_upper.png' % prefix, L.SHADE_FRAMES, h2, (gx + c * dw, gy, dw, gh // 2), as_dots))
            out2.append(filmstrip('%s %d lo' % (prefix, c + 1), '%s_lower.png' % prefix, L.SHADE_FRAMES, h2, (gx + c * dw, gy + gh // 2, dw, gh // 2), as_dots))
        return out2

    # Behind: the combined curve in grey. In front: the selected band's own curve in white (its dots reuse
    # the shading values).
    out += line_layer('para_bgline', 'PBgLine') + dot_layer('para_bgdot', 'PBgDot')
    out += line_layer('para_line', 'PLine') + dot_layer('para_dot', 'PShade')
    for b in range(L.PARA_BANDS):
        out.append(xypad('ring %d' % (b + 1), P('Sel Node X'), P('Sel Node Y'), 'para_ring_%d.png' % (b + 1),
                         L.node_pad(L.RING), 'IndexedEnabling/%d/%d/%s' % (b + 1, nsel, sel), 'No'))
    for b in range(L.PARA_BANDS):
        out.append(xypad('node %d' % (b + 1), P('Node %d X' % (b + 1)), P('Node %d Y' % (b + 1)), 'para_node_%d.png' % (b + 1),
                         L.node_pad(L.NODE), 'RangedEnabling/%d/%d/%d/%s' % (b, L.PARA_BANDS - 1, L.PARA_BANDS, bands), 'No'))
    out.append(xypad('para pen', P('Para Pen X'), P('Para Pen Y'), 'para_pen.png', L.node_pad(L.PEN)))

    out.append(segment('band none', 'pband_none', sel, 0, nsel, L.NONE_BTN))
    x0, y, w, h, gap = L.BAND_BTN
    for b in range(L.PARA_BANDS):
        out.append(segment('band %d' % (b + 1), 'pband_%d' % (b + 1), sel, b + 1, nsel, (x0 + b * (w + gap), y, w, h),
                           'RangedEnabling/%d/%d/%d/%s' % (b, L.PARA_BANDS - 1, L.PARA_BANDS, bands)))
    # BANDS: - removes the selected band, + adds one; the count between them.
    out.append(segment('band minus', 'pminus', P('Para Remove'), 1, 1, L.MINUS_BTN))
    out.append(segment('band plus', 'pplus', P('Para Add'), 1, 1, L.PLUS_BTN))
    out.append(value('band count', bands, L.BANDS_COUNT, 26, L.TEXT, 'horizontallyCentred verticallyCentred'))
    sx, sy, sw2, sh2, sgap = L.STYLE_SEGS
    for i, lab in enumerate(L.STYLE_LABELS):
        out.append(segment('style %s' % lab.lower(), 'pseg_%s' % lab.lower(), style, i, len(L.STYLE_LABELS),
                           (sx + i * (sw2 + sgap), sy, sw2, sh2)))
    out.append(segment('para analyzer', 'panalyzer', P('Analyzer'), 1, 1, L.PARA_ANALYZER))
    out.append(value('para info', P('Para Info'), L.PARA_INFO, 16))   # Nexa runs ~0.55 em per character: 31 chars fit 304 px
    for pname, (x, cap) in L.PARA_KNOBS.items():
        out.append(knob(cap.lower(), P(pname), (x, L.PARA_KNOB_Y, L.KNOB, L.KNOB)))
        out.append(value(cap.lower() + ' value', P(pname), (x + L.PARA_VALUE_DX, L.PARA_KNOB_Y + 24, L.PARA_VALUE_W, 40),
                         26, L.TEXT, 'left verticallyCentred'))
    tx, ty, tw, tth, tgap = L.TYPE_SEGS
    for i, lab in enumerate(L.TYPE_LABELS):
        out.append(segment('type %s' % lab.lower(), 'pseg_%s' % lab.lower().replace(' ', ''), P('Sel Type'), i,
                           len(L.TYPE_LABELS), (tx + i * (tw + tgap), ty, tw, tth)))
    return out


def main():
    types = ['Bell', 'Low Shelf', 'High Shelf', 'Low Cut', 'High Cut', 'Notch', 'Band Pass', 'Tilt']
    slopes = ['6 dB', '12 dB', '18 dB', '24 dB', '36 dB', '48 dB']
    reps = [{'key': P(n), 'value': ['Off', 'On']} for n in ('Clear', 'Undo', 'Bypass', 'Analyzer')]
    reps += [{'key': P('Para Sel'), 'value': ['None'] + [str(k) for k in range(1, 9)]},
             {'key': P('Para Add'), 'value': ['Off', 'On']}, {'key': P('Para Remove'), 'value': ['Off', 'On']},
             {'key': P('Para Bands'), 'value': [str(k) for k in range(1, 9)]},
             {'key': P('Sel Type'), 'value': ['Bell', 'Low Shelf', 'High Shelf', 'Low Cut', 'High Cut', 'Notch']},
             {'key': P('Sel Slope'), 'value': ['6 dB', '12 dB', '18 dB', '24 dB', '36 dB', '48 dB']},
             {'key': P('Para Style'), 'value': ['Dots', 'Line']}]
    reps += [{'key': P('Analyze'), 'value': ['Pre', 'Post']},
             {'key': P('Speed'), 'value': ['Slow', 'Medium', 'Fast']},
             {'key': P('Max Bands'), 'value': [str(k) for k in range(1, 9)]}]
    for b in range(1, 9):
        reps += [{'key': P('B%d On' % b), 'value': ['Off', 'On']},
                 {'key': P('B%d Type' % b), 'value': types},
                 {'key': P('B%d Slope' % b), 'value': slopes}]

    eq = eq_tab()
    para = para_tab()
    tui = {'pageData': {
        'version': 1,
        'componentDefinitions': {'version': 2, 'importFiles': [], 'localComponentDefinitions': []},
        'replacementStrings': reps,
        'info': {'version': 1, 'type': 'CompleteDescription'},
        # PARA is the first tab (the plugin opens on it); DRAW second.
        'tabs': [tab('PARA', 0, para, 'bg_para.png'), tab('DRAW', 1, eq, 'bg.png')]}}

    # Q-Links (4 x 4, Q-Link 13 top left). Both tabs and Program mode: Freq / Gain of all 8 bands,
    # two bands per row.
    def row_base(r):
        return 13 - 4 * r

    eq_q = {}
    for b in range(8):
        base = row_base(b // 2) + 2 * (b % 2)
        eq_q['Q-Link %d' % base] = IDX['B%d Freq' % (b + 1)]
        eq_q['Q-Link %d' % (base + 1)] = IDX['B%d Gain' % (b + 1)]

    def full(m):
        return {'Q-Link %d' % k: m.get('Q-Link %d' % k, -1) for k in range(1, 17)}

    # PARA: the selected band (through the proxies) on the top row.
    para_q = {'Q-Link 13': IDX['Sel Freq'], 'Q-Link 14': IDX['Sel Gain'], 'Q-Link 15': IDX['Sel Q'], 'Q-Link 16': IDX['Sel Slope']}

    prog = {}
    for b in range(8):
        prog['Q-Link %d' % (2 * b + 1)] = IDX['B%d Freq' % (b + 1)]
        prog['Q-Link %d' % (2 * b + 2)] = IDX['B%d Gain' % (b + 1)]
    qlinks = {'version': 4, 'info': {'version': 1, 'type': 'CompleteDescription'},
              'Screen Mode Q-Links': {'version': 4, 'map': [
                  {'Tab': 1, 'SubTab': 1, 'Bank Direction': 'Column', 'Q-Links': full(para_q)},
                  {'Tab': 2, 'SubTab': 1, 'Bank Direction': 'Column', 'Q-Links': full(eq_q)}]},
              'Program Mode Q-Links': full(prog)}

    os.makedirs(OUT, exist_ok=True)
    json.dump(tui, open(os.path.join(OUT, 'TUI.json'), 'w'), indent=2)
    json.dump(qlinks, open(os.path.join(OUT, 'Q-Links.json'), 'w'), indent=2)
    print('skins: TUI.json (DRAW %d, PARA %d components), Q-Links.json' % (len(eq), len(para)))


if __name__ == '__main__':
    main()
