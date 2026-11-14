# Recommendation after Experiment A: how EQ Artist's PARA page should take touches

Based on the Live III results in `../test-results/live3.md` (sessions 1 and 2, 2026-11-14) and the earlier
EQ Artist evidence (P1–P5). Verified facts and hypotheses are kept apart.

## What is verified on the Live III

| Fact | Source |
|---|---|
| A native XYPad drives two plugin parameters. A touch or tap jumps the puck to the finger (absolute position, not relative drag) | P4, A5 |
| The full range is reachable: 0.000–1.000 on both axes with the puck centre on the edges, when the pad overhangs its drawn area by half a puck. **Y = 1 is the top** | A2, A3, D2 |
| The puck is drawn where its values say, and it follows values the plugin sets, so a display-only XYPad works as a moving marker | D2, A8; Scratchivision cue markers |
| **Stacked pads: the top pad takes every touch**, even one placed exactly on a lower pad's puck. A lower pad can only be a display | C2–C4 |
| Side-by-side pads are independent, and a drag stays with the pad it started in, clamped at its edge | B1, B2 |
| **There is no release event:** updates stop when the finger stops moving, and the same when it lifts | A6 |
| The puck position survives a tab switch | A9 |
| About 230 parameter display updates per second display without lag on the device | P5 |
| A finger drag sends about 45 updates per second slowly and up to about 209 fast (X and Y counted separately: about 22–105 touch points per second) | A4 |
| Fixed-text Labels do not render; Value labels and images do | P2, P3 |

## Recommendation (option B in the research plan: native hybrid)

1. **One full-graph XYPad as the input, on top of everything**: the configuration already proven by the
   DRAW page's pen.
2. **Band nodes are display-only XYPads underneath**, one per band in its own colour, bound to node X/Y
   parameters the plugin sets. They are never touched directly: C2 shows they cannot be.
3. **Hit-testing in the plugin.** The first update of a new touch (after a pause in updates) picks the
   nearest node within a radius and grabs it, keeping the finger-to-node offset so the node does not jump
   to the finger. Later updates drag it. A touch on empty space selects nothing (or, later, could add a
   band).
4. **Ending a drag:** since there is no release (A6), "new touch" has to mean "updates resumed after a
   pause". A pause that is too short makes a slow drag drop the node; one that is too long makes a quick
   second tap re-grab the old node. Start with about 0.25 s, and keep the grab if the new touch lands on
   the node that was just held.
5. **Map nodes through the plugin, not straight to band parameters.** Node X/Y run 0–1 over the drawn
   graph (log 20 Hz–20 kHz, ±18 dB with Y = 1 at the top); the plugin converts to band frequency and gain
   and back. That keeps the graph's ±18 dB independent of the bands' ±24 dB range.
6. **Curve and band shading** stay as in `../../docs/PARA_STUDY.md`: 64-column film strips at 0.5 dB.
   While a node is dragged, the curve gets priority in the display budget. Touch input (up to about 105
   points per second) is much faster than the curve can redraw, so the display budget, not the input, sets
   how smoothly the curve follows. Node markers (2 parameters) go before the curve columns.

## Hypotheses still to verify (not evidence yet)

- Q-Links moving the node (A7), reopening the plugin and reloading the project (A10, A11), two instances
  (A12), multi-touch (B3), and audio and CPU while dragging (X1, X2).
- Whether a drawn line could ever be continuous. The skin has no runtime line drawing; `Path` and
  `Plotter` are Experiments C and D, still to run.
- MPC XL: nothing tested.

## What would make a true curve editor possible (host features that don't exist)

AIR Visual EQ4, which ships with the MPC, draws a smooth vector EQ curve. It is part of the MPC
software, so it draws with the app's own code; third-party plugins only get the documented skin widgets.
Of those, Path (fixed geometry, the value has no effect: L2) and Plotter (draws nothing: L3) cannot draw a
curve that follows values. Envelope draws smooth vector lines from plugin values (E1, E2) but cannot follow
an EQ curve (G1, G2: Pitch is straight-only with a fixed flat sustain stretch; ADSR bends but has a fixed
shape). The film-strip line works (K7d), but it costs about 124 MB of images per colour.

- A release (touch-up) event, or press state, from XYPad.
- Touches reaching overlapped widgets, or a multi-point "node" widget.
- A widget that draws a polyline from an array parameter, or the response-curve widget AIR Visual EQ4 uses,
  opened to plugins (bound to band parameters, or fed points by the plugin).
