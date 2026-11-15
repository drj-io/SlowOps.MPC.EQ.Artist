#!/usr/bin/env python3
"""Write plugins/<EQA_VARIANT>/skins/TUI.json and Q-Links.json (EQ Artist Dots or EQ Artist Lines).

Standard library only. Hand-built skin; the SDK's build.py copies skins/ verbatim. Parameter indices
come from plugins/<variant>/build/params.json (written by `build.py --target native`), looked up by
name, so the skin always matches the binary. Layout and palette come from skin_layout.py, the PNGs from
the art scripts (scripts/make_art.sh).

Both plugins have the same two tabs; only the curve layers differ (curve_layer()):
  dots   a curve is 64 dot columns, each an upper and a lower half strip
  lines  a curve is 76 line columns, each 3 stacked tiles

PARA (the first tab, back to front): spectrum; the selected band's flat fill; the combined curve in grey
("Curve n"); the selected band's curve in white ("Front n"); selection rings; band nodes (display-only
XYPads); the pen (one transparent XYPad over the graph, hit-tested by the plugin: stacked pads cannot be
separate nodes on the MPC); DRAW's CLEAR / UNDO / BYPASS / ANALYZER, NONE / band buttons; TYPE (3 x 2), - / + bands, Q, SLOPE, and DRAW's
ANALYZER PRE | POST and SPEED switches in the same place as on DRAW; the status under PRE | POST.
DRAW: spectrum; the EQ you hear in white (the same "Curve n" values); your drawing as rings ("Draw n");
the pen; CLEAR / UNDO / BYPASS / ANALYZER, status; BANDS - n + (PARA's place), PRE | POST, SLOW | MED | FAST.
Fixed text does not render on the MPC, so every caption is drawn into the images.
Every setting is a focus slot (slot()): touch it and a ring shows, and the MPC's data wheel edits it.
Q-Links: PARA = the selected band's Freq, Gain, Q, Slope; DRAW and Program mode = Freq / Gain of bands 1-8.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skin_layout as L  # noqa: E402

params = json.load(open(os.path.join(L.PROJECT, 'build', 'params.json')))['params']
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


def button(name, handle, r, img=None):
    img = img or name
    return entry(name, 'Button', {'version': 2, 'onImage': img + '_on.png', 'offImage': img + '_off.png',
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
    # The pens are exactly the entry the hardware spike used (version 1 bounds, hardware focus on).
    return entry(name, 'XYPad', {'version': 1, 'xPositionHandle': x_handle, 'yPositionHandle': y_handle,
                                 'image': img}, r, 'Show', focus, only)


DEFS = []   # local component definitions: one per focus slot


def slot(key, r, children, pname):
    """Wrap a control group (entries with absolute tab rects) in a focusable custom component bound to pname: one
    hardware-focus stop with a ring shown while focused, so the data wheel edits pname. Children are moved into the
    slot's coordinates, lose their own focus, and their pname handle becomes the slot's "Data"; any other handle
    (the BANDS buttons' Add / Remove, a visibility rule) passes through unchanged."""
    x0, y0, w, h = r
    handle = P(pname)
    kids = [entry('focus', 'Focus', {'version': 1, 'backgroundColour': L.FOCUS_BG, 'outlineColour': L.FOCUS_RING,
                                     'backgroundInset': 4.0, 'outlineThickness': 2.0}, (0, 0, w, h))]
    kids[0]['bounds']['whenVisible'] = 'WhenFocussed'
    for c in children:
        b = c['bounds']
        cx, cy, cw, ch = [float(v) for v in b['bounds'].split()]
        b['bounds'] = rect((cx - x0, cy - y0, cw, ch))
        b['acceptsHWFocus'] = 'No'
        d = c['componentData']['data']
        if d.get('handleName') == handle:
            d['handleName'] = 'Data'
        kids.append(c)
    DEFS.append({'key': key, 'value': {
        'version': 2,
        'actions': [{'version': 1, 'onAction': 'Mouse Down', 'handler': 'Q-Link', 'handleName': 'Data', 'additionalData': ''}],
        'backgroundData': {'version': 1, 'focussed': {'version': 1, 'colour': '0', 'image': ''},
                           'unfocussed': {'version': 1, 'colour': '0', 'image': ''}},
        'ignoreMousePresses': False, 'disableCoarseDataWheel': False, 'componentsData': kids}})
    e = entry(key, key, {'version': 1, 'handleName': 'Data'}, r, 'Hide', 'Yes')
    e['handle remapping']['map'] = [{'key': 'Data', 'value': handle}]
    return e


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


def spectrum():
    return [filmstrip('spec %d' % (c + 1), 'spec_bar.png', L.SPEC_FRAMES, P('Spec %d' % (c + 1)), col_rect(c))
            for c in range(L.NUM_COLS)]


def curve_layer(look, pname):
    """A curve bound to "<pname> n". look: 'front' (white) or 'back' (grey)."""
    gx, gy, gw, gh = L.GRAPH
    out = []
    if L.LINES:
        prefix = 'para_line' if look == 'front' else 'para_bgline'
        w, th = gw // L.LINE_COLS, gh // L.LINE_TILES
        frames = L.LINE_LEVELS * len(L.LINE_RISES)
        for c in range(L.LINE_COLS):
            for t in range(L.LINE_TILES):
                out.append(filmstrip('%s %d t%d' % (pname.lower(), c + 1, t), '%s_t%d.png' % (prefix, t), frames,
                                     P('%s %d' % (pname, c + 1)), (gx + c * w, gy + t * th, w, th)))
    else:
        prefix = 'para_dot' if look == 'front' else 'para_bgdot'
        w = gw // L.SHADE_COLS
        for c in range(L.SHADE_COLS):
            h = P('%s %d' % (pname, c + 1))
            out.append(filmstrip('%s %d up' % (pname.lower(), c + 1), prefix + '_upper.png', L.SHADE_FRAMES, h, (gx + c * w, gy, w, gh // 2)))
            out.append(filmstrip('%s %d lo' % (pname.lower(), c + 1), prefix + '_lower.png', L.SHADE_FRAMES, h, (gx + c * w, gy + gh // 2, w, gh // 2)))
    return out


def draw_tab():
    gx, gy, gw, gh = L.GRAPH
    out = spectrum()
    out += curve_layer('front', 'Curve')   # the EQ you hear
    out += [filmstrip('draw %d' % (c + 1), 'draw_dot.png', L.DOT_FRAMES, P('Draw %d' % (c + 1)), col_rect(c))
            for c in range(L.NUM_COLS)]
    # The pen. The puck stays inside its pad, so the pad overhangs the graph by half a puck on every side:
    # the puck's centre then reaches the graph's edges.
    h = L.PUCK / 2.0
    out.append(xypad('pen', P('Pen X'), P('Pen Y'), 'puck.png', (gx - h, gy - h, gw + 2 * h, gh + 2 * h)))
    for (pname, caption), x in zip(L.BUTTONS, L.BUTTON_X):
        out.append(button(caption.lower(), P(pname), (x, L.BUTTON_Y, L.BUTTON_W, L.BUTTON_H)))
    out.append(value('fit info', P('Fit'), L.FIT_INFO))
    for (caption, kind, pname), x in zip(L.BOTTOM, L.BLOCK_X):
        if kind == 'knob':
            vx, vy, vw, vh = L.KNOB_VALUE
            fdx, fy, fw, fh = L.FOCUS_KNOB_SLOT
            out.append(slot('draw %s slot' % caption.lower(), (x + fdx, fy, fw, fh), [
                knob(caption.lower(), P(pname), (x, L.KNOB_Y, L.KNOB, L.KNOB)),
                value(caption.lower() + ' value', P(pname), (x + vx, vy, vw, vh), 28, L.TEXT, 'left verticallyCentred')], pname))
        elif kind == 'bands':   # PARA's - n + in DRAW's place: here they step the count and refit the drawing
            dx = x - L.MINUS_BTN[0]
            shift = lambda r: (r[0] + dx,) + tuple(r[1:])
            fx, fy, fw, fh = L.PARA_FOCUS['Bands']
            out.append(slot('draw bands slot', (fx + dx, fy, fw, fh), [
                segment('draw band minus', 'pminus', P('Fewer Bands'), 1, 1, shift(L.MINUS_BTN)),
                segment('draw band plus', 'pplus', P('More Bands'), 1, 1, shift(L.PLUS_BTN)),
                value('draw band count', P(pname), shift(L.BANDS_COUNT), 26, L.TEXT, 'horizontallyCentred verticallyCentred')], pname))
    return out + analyzer_switches('')


def analyzer_switches(prefix):
    """ANALYZER PRE | POST and SPEED SLOW | MED | FAST: the same place on both tabs (radio groups of image buttons),
    each one focus slot."""
    out = []
    for (caption, kind, pname), x in zip(L.BOTTOM, L.BLOCK_X):
        if kind != 'switch':
            continue
        labels, w = L.SWITCHES[pname]
        segs = [segment('%s%s %s' % (prefix, caption.lower(), lab.lower()), 'seg_' + lab.lower(), P(pname), i, len(labels),
                        (x + i * (w + 6), L.SEG_Y, w, L.SEG_H)) for i, lab in enumerate(labels)]
        width = len(labels) * w + (len(labels) - 1) * 6
        out.append(slot('%s%s slot' % (prefix or 'draw ', caption.lower()), (x - 6, L.FOCUS_SWITCH_Y, width + 12, L.FOCUS_SWITCH_H), segs, pname))
    return out


def para_tab():
    gx, gy, gw, gh = L.GRAPH
    sel, bands = P('Sel Band'), P('Bands')
    nsel = L.PARA_BANDS + 1   # Sel Band: 0 = none, 1..8 = a band
    out = spectrum()
    fw = gw // L.CURVE_COLS
    nfill = len(L.PARA_COLOURS) * L.FILL_LEVELS
    out += [filmstrip('fill %d' % (c + 1), 'para_fill.png', nfill, P('Fill %d' % (c + 1)), (gx + c * fw, gy, fw, gh))
            for c in range(L.CURVE_COLS)]
    out += curve_layer('back', 'Curve') + curve_layer('front', 'Front')
    for b in range(L.PARA_BANDS):
        out.append(xypad('ring %d' % (b + 1), P('Sel Node X'), P('Sel Node Y'), 'para_ring_%d.png' % (b + 1),
                         L.node_pad(L.RING), 'IndexedEnabling/%d/%d/%s' % (b + 1, nsel, sel), 'No'))
    shown = ['RangedEnabling/%d/%d/%d/%s' % (b, L.PARA_BANDS - 1, L.PARA_BANDS, bands) for b in range(L.PARA_BANDS)]
    for b in range(L.PARA_BANDS):
        out.append(xypad('node %d' % (b + 1), P('Node %d X' % (b + 1)), P('Node %d Y' % (b + 1)), 'para_node_%d.png' % (b + 1),
                         L.node_pad(L.NODE), shown[b], 'No'))
    out.append(xypad('para pen', P('Para Pen X'), P('Para Pen Y'), 'para_pen.png', L.node_pad(L.PEN)))

    x0, y, w, h, gap = L.BAND_BTN
    out.append(slot('para band select slot', L.PARA_FOCUS['Sel Band'],
                    [segment('band none', 'pband_none', sel, 0, nsel, L.NONE_BTN)] +
                    [segment('band %d' % (b + 1), 'pband_%d' % (b + 1), sel, b + 1, nsel, (x0 + b * (w + gap), y, w, h), shown[b])
                     for b in range(L.PARA_BANDS)], 'Sel Band'))
    # BANDS: - removes the selected band, + adds one; the count between them. The data wheel sets the count.
    out.append(slot('para bands slot', L.PARA_FOCUS['Bands'], [
        segment('band minus', 'pminus', P('Remove Band'), 1, 1, L.MINUS_BTN),
        segment('band plus', 'pplus', P('Add Band'), 1, 1, L.PLUS_BTN),
        value('band count', bands, L.BANDS_COUNT, 26, L.TEXT, 'horizontallyCentred verticallyCentred')], 'Bands'))
    for (pname, caption), x in zip(L.BUTTONS, L.BUTTON_X):   # DRAW's top row, same place, same switches
        out.append(button('para ' + caption.lower(), P(pname), (x, L.BUTTON_Y, L.BUTTON_W, L.BUTTON_H), caption.lower()))
    out.append(value('para info', P('Para Info'), L.PARA_INFO, 16, L.TEXT_DIM, 'left verticallyCentred'))
    for pname, (x, cap) in L.PARA_KNOBS.items():
        vdx, vy, vw, vh = L.PARA_VALUE
        out.append(slot('para %s slot' % cap.lower(), L.PARA_FOCUS[pname], [
            knob(cap.lower(), P(pname), (x, L.PARA_KNOB_Y, L.KNOB, L.KNOB)),
            value(cap.lower() + ' value', P(pname), (x + vdx, vy, vw, vh), 20, L.TEXT, 'horizontallyCentred verticallyCentred')], pname))
    out += analyzer_switches('para ')
    # GAIN / SLOPE: slots in one place, one shown per band type (Sel Type: 0-2 bell and shelves, 3-4 cuts, 5 notch,
    # whose gain is its depth). A visibility rule is one range, so the notch has its own GAIN slot.
    stype = P('Sel Type')
    sx = L.SHARED_X
    vdx, vy, vw, vh = L.PARA_VALUE
    for pname, cap, lo, hi in (('Sel Gain', 'gain', 0, 2), ('Sel Slope', 'slope', 3, 4), ('Sel Gain', 'gain', 5, 5)):
        e = slot('para %s slot%s' % (cap, ' notch' if lo == 5 else ''), L.PARA_FOCUS['Shared'], [
            filmstrip('%s caption' % cap, 'pcap_%s.png' % cap, 1, stype, L.SHARED_CAPTION),
            knob(cap, P(pname), (sx, L.PARA_KNOB_Y, L.KNOB, L.KNOB)),
            value(cap + ' value', P(pname), (sx + vdx, vy, vw, vh), 20, L.TEXT, 'horizontallyCentred verticallyCentred')], pname)
        e['bounds']['version'] = 2
        e['bounds']['additionalInvalidatingHandles'] = ['RangedEnabling/%d/%d/%d/%s' % (lo, hi, len(L.TYPE_LABELS), stype)]
        out.append(e)

    tx, ty, tw, tth, tgap, tcols = L.TYPE_GRID
    types = []
    for place, i in enumerate(L.TYPE_ORDER):   # the button's id stays the type; only its place changes
        lab = L.TYPE_LABELS[i]
        r, c = divmod(place, tcols)
        types.append(segment('type %s' % lab.lower(), 'pseg_%s' % lab.lower().replace(' ', ''), P('Sel Type'), i,
                             len(L.TYPE_LABELS), (tx + c * (tw + tgap), ty + r * (tth + tgap), tw, tth)))
    out.append(slot('para type slot', L.PARA_FOCUS['Sel Type'], types, 'Sel Type'))
    return out


def main():
    types = ['Bell', 'Low Shelf', 'High Shelf', 'Low Cut', 'High Cut', 'Notch']
    slopes = ['6 dB', '12 dB', '18 dB', '24 dB', '36 dB', '48 dB']
    counts = [str(k) for k in range(1, 9)]
    reps = [{'key': P(n), 'value': ['Off', 'On']} for n in ('Clear', 'Undo', 'Bypass', 'Analyzer', 'Add Band', 'Remove Band',
                                                         'More Bands', 'Fewer Bands')]
    reps += [{'key': P('Sel Band'), 'value': ['None'] + counts},
             {'key': P('Bands'), 'value': counts},
             {'key': P('Max Bands'), 'value': counts},
             {'key': P('Sel Type'), 'value': types},
             {'key': P('Sel Slope'), 'value': slopes},
             {'key': P('Analyze'), 'value': ['Pre', 'Post']},
             {'key': P('Speed'), 'value': ['Slow', 'Medium', 'Fast']}]
    for b in range(1, 9):
        reps += [{'key': P('B%d On' % b), 'value': ['Off', 'On']},
                 {'key': P('B%d Type' % b), 'value': types},
                 {'key': P('B%d Slope' % b), 'value': slopes}]

    para, draw = para_tab(), draw_tab()
    tui = {'pageData': {
        'version': 1,
        'componentDefinitions': {'version': 2, 'importFiles': [], 'localComponentDefinitions': DEFS},
        'replacementStrings': reps,
        'info': {'version': 1, 'type': 'CompleteDescription'},
        # PARA is the first tab (the plugin opens on it); DRAW second.
        'tabs': [tab('PARA', 0, para, 'bg_para.png'), tab('DRAW', 1, draw, 'bg.png')]}}

    # Q-Links (4 x 4, Q-Link 13 top left).
    def full(m):
        return {'Q-Link %d' % k: m.get('Q-Link %d' % k, -1) for k in range(1, 17)}

    para_q = {'Q-Link 13': IDX['Sel Freq'], 'Q-Link 14': IDX['Sel Gain'], 'Q-Link 15': IDX['Sel Q'], 'Q-Link 16': IDX['Sel Slope']}
    draw_q, prog = {}, {}
    for b in range(8):   # DRAW: two bands per row; Program mode: in order
        base = 13 - 4 * (b // 2) + 2 * (b % 2)
        draw_q['Q-Link %d' % base] = IDX['B%d Freq' % (b + 1)]
        draw_q['Q-Link %d' % (base + 1)] = IDX['B%d Gain' % (b + 1)]
        prog['Q-Link %d' % (2 * b + 1)] = IDX['B%d Freq' % (b + 1)]
        prog['Q-Link %d' % (2 * b + 2)] = IDX['B%d Gain' % (b + 1)]
    qlinks = {'version': 4, 'info': {'version': 1, 'type': 'CompleteDescription'},
              'Screen Mode Q-Links': {'version': 4, 'map': [
                  {'Tab': 1, 'SubTab': 1, 'Bank Direction': 'Column', 'Q-Links': full(para_q)},
                  {'Tab': 2, 'SubTab': 1, 'Bank Direction': 'Column', 'Q-Links': full(draw_q)}]},
              'Program Mode Q-Links': full(prog)}

    os.makedirs(L.SKINS, exist_ok=True)
    json.dump(tui, open(os.path.join(L.SKINS, 'TUI.json'), 'w'), indent=2)
    json.dump(qlinks, open(os.path.join(L.SKINS, 'Q-Links.json'), 'w'), indent=2)
    print('%s skins: TUI.json (PARA %d, DRAW %d components), Q-Links.json' % (L.VARIANT, len(para), len(draw)))


if __name__ == '__main__':
    main()
