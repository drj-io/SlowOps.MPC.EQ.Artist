# MPC Live III results: Experiment A, native XYPad

Device: MPC Live III, MPC OS 3.10.1.12 (see `../research/sources.md`). Build: Curve UI Test 0.1.0.
Results: **PASS**, **FAIL**, **PARTIAL** or **NOT TESTED**. A result is only PASS from an observation on
the MPC itself, never from the simulator or validation. Put screenshots or short videos in `evidence/` and
name them by check number (for example `A3.png`).

## Prior evidence (from EQ Artist on the same device, 2026-11-14)

| # | Observation | Result | Source |
|---|---|---|---|
| P1 | Network install with `mpc_remote.py`; the hand-written skin parses and its tabs show | PASS | EQ Artist 0.1–0.4 installs |
| P2 | Value Labels (bound to a parameter) render | PASS | EQ Artist status line ("5 bands, 0.5 dB off" in your screenshot) |
| P3 | Fixed-text Labels (`type: "Text"`, handle `Invalid`) render | **FAIL**: nothing drawn | EQ Artist 0.2 axis and button labels missing in your screenshot |
| P4 | A transparent XYPad over a 1216 × 432 graph drives its two parameters and the plugin acts on them | PASS | EQ Artist pen: drawing works, chosen over sliders |
| P5 | About 230 parameter display updates per second display without lag on the device | PASS | EQ Artist 0.3.2 / 0.3.3 NORMAL; the lag seen was the remote viewer |

## Session 1 (2026-11-14, screenshots `evidence/full-1.png`, `split-1.png`, `stack-1.png`, `safe-1.png`)

| # | Observation | Result |
|---|---|---|
| D1 | Curve UI Test loads; all four tabs (FULL, SPLIT, STACK, SAFE) show, with every caption drawn into the backgrounds, Value labels, image buttons, pad panels and pucks as designed | PASS |
| D2 | The puck sits where its values say: Pad A X 0.325 is 32 % across the panel, Y 0.630 is 37 % down from the top. So **Y = 1 is the top edge**, and the puck follows the parameters (the inverse mapping works) | PASS |
| D3 | Pad A reported X 0.072–0.576, Y 0.352–0.811 so far: touches register, but the corners were not tried yet (A2 still open) | PARTIAL |
| D4 | SPLIT and STACK show their pads at their start values (B, C, D at 0.5; E at X 0.7); no touches on them yet | NOT TESTED |
| D5 | STACK, via the Remote Screen viewer (`evidence/stack-2-remote.png`): a touch in the shared rectangle moved **E** (the top pad) to 0.315 / 0.657; D stayed at 0.500 / 0.500; LAST PAD = E. Where exactly the touch landed relative to D's puck is not recorded | PARTIAL (see C1, C2) |

## Session 2 (2026-11-14): Claude desktop, through the Remote Screen viewer (mouse as touch)

Run by Claude (computer use) in the "Remote Screen - MPC" app with remote control on. Mouse presses go to the MPC
as touches. Readings were taken from screenshots (raw report kept in the conversation; screenshot IDs refer to
that session's captures). **Caveat:** these are viewer-mouse touches, not fingers. Rates in particular are the
viewer's, not the touchscreen's.

| # | Result | Evidence |
|---|---|---|
| C2 | **FAIL for "stacked pads as separate nodes":** pressing exactly on D's puck moved **E** (the top pad), which jumped to the touch; D never moved | LAST PAD E; D stayed 0.500 / 0.500 |
| C3 | Pressing on E's puck moves E (expected) | E Y 0.498 → 0.172 |
| C4 | Pressing on empty space moves E (it jumps to the touch) | E → 0.892 / 0.048 |
| A1 / A5 | **PASS:** a single tap (no drag) registers and the puck jumps to it | A → 0.500 / 0.498 |
| A2 | **PASS:** all four corners reach exactly 0.000 and 1.000 on both axes; **Y = 1 is the top** | X MIN 0.000, X MAX 1.000, Y MIN 0.000, Y MAX 1.000 |
| A3 | **PASS:** along the top edge the puck centre reaches both edge lines | X 0.002 → 0.998 at Y 0.989 |
| A6 | **No release signal:** updates simply stop. GAP MS climbs the same while the finger is held still and after it lifts, so the plugin cannot tell them apart | GAP 1420 → 2570 held, 2770 → 5860 released |
| A4 | Viewer-mouse rate 16–18 updates/s (28–30 with no pauses between mouse moves). Finger rate on the touchscreen still **NOT TESTED** | UPDATES/S 16, 18 |
| A8 | **PASS:** the puck follows values the plugin sets (Auto Move circles it, and it stops when Auto Move is off). GAP MS stays 9999 by design: the plugin's own moves are not counted as touches | X/Y changing each second |
| A9 | **PASS:** after a tab switch the puck and values are unchanged | 0.395 / 0.886 before and after |
| B1 | **PASS:** each side-by-side pad drives only its own pair | LAST PAD B then C |
| B2 | **PASS:** a drag that starts in B stays with B, clamps at B's edge, and never reaches C, even when it crosses into C | B X 1.000; C unchanged |
| Other | While pressed, the host draws the puck with a larger ring (its own pressed state). On SPLIT the host's INSERT −/+ buttons drew as blank squares (host chrome, not the plugin). The first tab switch to SPLIT once showed FULL's content for a moment. No dialogs | report notes |

Still open: multi-touch (B3), Q-Links following (A7), reopen and
project reload (A10, A11), two instances (A12), audio dropouts and CPU (X1, X2).

## Setup

1. Save your project. Restart the MPC app if asked (the new plugin needs a rescan).
2. On an audio track with sound playing, add **Curve UI Test** (External Plugins) as an insert.
3. You should hear the audio unchanged. If it drops out at any point, note when.

## FULL tab (Pad A)

| # | Do | Look at | Result | Notes |
|---|---|---|---|---|
| A1 | Touch the pad centre and hold still | The puck jumps to your finger? PAD A X/Y ≈ 0.5 | PARTIAL | Touches move Pad A, and the puck matches the values (D2); jump-to-finger not yet confirmed |
| A2 | Touch each corner (start with RESET) | X MIN / X MAX / Y MIN / Y MAX: do they reach 0.000 and 1.000? Which corner is Y = 1 (top or bottom)? | PARTIAL | Y = 1 is the top (D2). Corners not tried yet |
| A3 | Drag slowly along each edge | Does the puck reach the panel's edge line? | | |
| A4 | Drag quickly in circles for a few seconds | UPDATES/S while dragging (write the number); does the puck keep up? | PASS | By finger (2026-11-14): about 45 dragging slowly, peaks about 209 moving fast. UPDATES/S counts X and Y separately, so that's about 22–105 touch points per second |
| A5 | Tap once without moving | Does the tap alone change X/Y (UPDATES/S goes up), or only a drag? | | |
| A6 | Drag, then lift and wait | GAP MS climbs after lifting? (Also try holding still without lifting: does it climb too?) | | |
| A7 | Turn Q-Links 13 and 14 | Does the puck follow X / Y? | | |
| A8 | AUTO MOVE on | Does the puck circle smoothly by itself? Does touching the pad fight it? | | |
| A9 | Switch to another tab and back | Puck where you left it? | | |
| A10 | Close the plugin window, reopen it | Puck where you left it? | | |
| A11 | Save the project, reload it | Pad A X/Y restored? | | |
| A12 | Add a second Curve UI Test on another track | Each instance's pad independent? | | |

## SPLIT tab (Pads B, C)

| # | Do | Look at | Result | Notes |
|---|---|---|---|---|
| B1 | Drag in Pad B, then in Pad C | LAST PAD shows B then C; only the touched pad's X/Y change | | |
| B2 | Start a drag in B and slide into C | Does B keep following (clamped at its edge), or does C take over? | | |
| B3 | Touch both pads at once (two fingers) | Do both move (multi-touch)? | | |

## STACK tab (Pads D under, E on top)

| # | Do | Look at | Result | Notes |
|---|---|---|---|---|
| C1 | Drag anywhere in the panel | LAST PAD: E only, D only, or both? | PARTIAL | One touch (remote viewer) went to E only (D5) |
| C2 | Touch exactly on D's puck | Which pad moves? | | |

**C1/C2 decide EQ Artist's node design:** if only the top pad ever gets touches, separate pads cannot be
separate band nodes, and the nodes must be one pad plus hit-testing in the plugin.

## SAFE tab

| # | Do | Look at | Result | Notes |
|---|---|---|---|---|
| S1 | Move Level, Slider 1, Slider 2 | They work (the known-good fallback) | | |

## Audio

| # | Check | Result | Notes |
|---|---|---|---|
| X1 | Audio passed through without dropouts during all of the above | | |
| X2 | MPC CPU meter with the plugin open, while dragging | | |

## CURVE tab (0.2.0): does a film-strip EQ curve render?

A coral bell (node "1") over the graph. White dots + light fill = the 64-column curve; coral shading = the
band's own 32-column shape. Q knob; STRIP HALVES | TALL; REFRESH 8 | 16 | 24 | 32 | ALL; SWEEP.
(Simulator: a sweeping bell needs about 490 curve updates/s; REFRESH 8 caps at about 232/s, 16 at about 457/s.)

| # | Do | Look at | Result | Notes |
|---|---|---|---|---|
| K1 | Open CURVE (STRIP HALVES, REFRESH 16) | Is there a bell-shaped curve of dots with a light fill and coral shading under it? Any blank, cut-off or garbled columns? Does the curve sit on the grid (peak at the node)? | PASS | `evidence/K1-curve-halves.png` (remote viewer, REFRESH ALL): 794 Hz, +12.2 dB, Q 1.73; 64 dots with fill and the coral 32-column shading render correctly, no blank or garbled columns, the half strips join invisibly at 0 dB, the node sits on the peak. You: "not bad", but you want a real line |
| K2 | Drag the node around (also to the top and bottom edges) | Does the curve follow? How far behind is it? Does the node stay on the curve's peak at +18 / -18 dB? | | |
| K3 | Turn Q | Does the bell get narrower / wider? | | |
| K4 | STRIP TALL | Same curve, no blank or garbled columns? (TALL strips are 31,536 px: tests the texture limit) | PASS | Your screenshot in the conversation (2026-11-14; not saved as a file): identical curve from single 19 × 31,536 px strips (73 frames × 432 px), no blank or garbled columns. **Strips at least 31,536 px tall render**: the 16,384 px limit assumed in PARA_STUDY does not apply here |
| K5 | SWEEP on, then try REFRESH 8, 16, 24, 32, ALL | Which setting looks smooth? Does any setting make the screen or touch lag? Note CURVE UPD/S for each | | |
| K6 | Look closely at a flat stretch and at the bell's sides | Does it read as a line, or as separate dots / steps? | | |

## CURVE tab, LINE mode (0.3.1): a connected line

STRIP has a third option, **LINE**: a continuous polyline (64 segments joined end to end at the column
boundaries, 0.5 dB steps) with the fill under it. Its images are 19 × 110,376 px (1,533 frames, 6 per
column, about 50 MB decoded), far past the 31,536 px proven in K4. **This tests whether very tall images work.**

| # | Do | Look at | Result | Notes |
|---|---|---|---|---|
| K7 | STRIP LINE (0.3.1: 0.5 dB steps, 110,376 px images, about 50 MB) | Continuous line, no blank or garbled columns, tab not slow? | PASS | Renders on the MPC: "looks pretty good but a little bit jagged" (2026-11-14). Images about 3.5x past the 31,536 px proven in K4 work |
| K7b | STRIP LINE (0.3.2: 0.25 dB steps, 198,360 px images, about 90 MB) | Smoother? Blank or garbled columns? Slow? | PASS (renders, smoother) / FAIL (narrow peaks) | Your screenshot (282 Hz, +18 dB, Q 4.08): renders cleanly and smoother than 0.3.1. The MPC memory meter showed about 10 %. **The line stopped at about +14 dB**: 5 dB per column was too little rise for the steep sides, and peaks between boundaries were missed |
| K7c | STRIP LINE (0.3.3: 76 columns of 16 px, rises up to 10 dB, each vertex takes the curve's extreme within half a column; 261,000 px images, about 100 MB) | Does a narrow +18 dB bell now reach its peak? Smooth? Memory meter? | PASS | `evidence/K7c-line-033-q10.png`: 173 Hz, +14.6 dB, **Q 10 (the narrowest)**: the line reaches the node with a clean peak; smooth tails; renders after restart. Memory meter not recorded. **Note:** the 32-column coral shading (1 dB, 4 samples per column) shows only about +9.5 dB for this narrow bell: it misses narrow peaks. Shading needs the line's peak-aware sampling or more columns |
| K7d | STRIP LINE (0.4.0: 152 columns of 8 px, 0.125 dB, 8,959-frame images 645,048 px tall, about 124 MB; CURVE tab 1,152 widgets) | Curvier than 0.3.3? Blank or garbled columns? CURVE tab slow to open? Memory meter? Smooth while dragging / SWEEP at REFRESH 16, 32, ALL? | PASS ("ok, not perfect") | `evidence/K7d-line-040-drag.png` (460 Hz, +16 dB, Q 10, mid-drag at REFRESH 16: CURVE UPD/S 146) and `K7d-line-040-q10.png` (365 Hz, +18 dB, Q 10): smooth, curved sides, peak reaches the node, no blank or garbled columns. **645,048 px images and 1,152 widgets work**; the tab "opened fast enough, a little delay". The 16-sample shading now reaches the peak too. Memory not re-read (0.3.3: 11–14 %) |
| K8 | Drag the node; try a narrow Q and a cut (drag down) | Does the line stay joined while it moves? How does it compare to the dots (HALVES)? | | |
| K9 | SWEEP on, REFRESH 16 and ALL | Smooth? Any lag or stutter (the MPC drawing tall images)? | | |

## LINES tab (0.2.0): native Path and Plotter

| # | Do | Look at | Result | Notes |
|---|---|---|---|---|
| L1 | Open LINES | Box 1: a flat line? Box 2: a smooth bell curve? (Do they draw at all, anti-aliased, inside their boxes?) | PARTIAL | `evidence/L1-lines.png` (SWEEP on, Line Value 0.947): boxes 1 and 2 (Path with handle `Invalid`) draw **nothing**. Box 3 (the same bell Path bound to Line Value) draws a **smooth, anti-aliased line**, but at **half size**: 300 × 55 px of the 600 × 110 path, from the box's top-left. So a Path only draws when bound to a valid parameter, and its coordinates come out ×0.5 (or it is scaled by something) |
| L2 | Move the LINE VALUE slider (or SWEEP on) | Box 3 (the Path bound to Line Value): does anything change (size, position, visibility)? Describe | FAIL | `evidence/L2-path-value-1.png`, `L2-path-value-0.png`: the bound Path is **pixel-identical at Line Value 1.000 and 0.000** (also at 0.947 in L1). The value does not scale, move or hide it; binding only makes it draw. Its geometry is fixed in the skin (drawn at half the coordinates given) |
| L3 | Same | Plotter: does it draw a line that tracks Line Value, scrolling left with history? | FAIL | Nothing drawn with SWEEP on (`evidence/L1-lines.png`); the Plotter entry follows ui-reference §6.16 version 2 |

## ENV tab (0.5.0): native Envelope widgets (Experiment B)

Three native `Envelope` widgets, every point bound to a plugin parameter: **ADSR** (the reference example,
with header; its curve shapes bound too), **Pitch** (9 points), **AD** (no header, no fill). SWEEP makes the
plugin move every point. Question: do they draw smooth vector curves that follow plugin-set values, and how
freely can the points move? (If Curve UI Test opens with **no tabs at all**, an Envelope entry was rejected.)

| # | Do | Look at | Result | Notes |
|---|---|---|---|---|
| E1 | Open ENV | Do all three envelopes draw? Smooth anti-aliased curves (like AIR Visual EQ4) or not? | PASS | `evidence/E1-env.png`: all three draw (the skin with Pitch and AD loads; 7 tabs). **AD draws smooth anti-aliased vector curves** (a curved rise and an exponential fall). ADSR (shapes bound at 0.5) and Pitch (shapes unbound) draw straight segments. Handles are drawn as small labelled circles; the header shows the name. Pitch: 6 vertices whose levels (y) and stage widths (x) are all parameters; the widths are not scaled to fill the box (the curve used about 225 of 600 px) |
| E2 | SWEEP on | Do the curves follow the plugin's values smoothly? Any lag or flicker? | PASS | "sweeps move smoothly" (2026-11-14): the native envelopes redraw smoothly from plugin-set values (11 points per sweep step, 40 steps a second) |
| E3 | Drag points on Pitch | Which points move up/down, which left/right, which both? Are they forced into order (left to right)? | | |
| E4 | Watch ADSR while SWEEP runs | Do the curve shapes (bent / straight) change as Env Shape A/D/R move? | FAIL | "ADSR lines never bend, they only have rigid corners": with `curveType` Quadratic, `forceCurveShape` 0.5 and `forceHideShapeHandles` true, the bound shape parameters have no visible effect. AD (shapes unbound) did draw curves |
| E5 | AD | What shape is it: a rise and fall like a bell? Where do its ends sit? | | |

## ENV2 tab (0.5.1): where envelope points land, and can lines bend?

Top: one long Pitch envelope with its handles, scale and background hidden (zero-size, transparent), over a
pixel ruler (a label every 100 px). Its 5 levels zigzag 10 % / 90 %, and **every stage is as long as the
WIDTH slider**. Below: Pitch and ADSR envelopes with `curveType` Exponential, their shape handles shown and
bound to the BEND slider. (If the plugin opens with **no tabs**, the zero-size handles were rejected.)

| # | Do | Look at | Result | Notes |
|---|---|---|---|---|
| G1 | ENV2, WIDTH at 0.25, then 0.50, then 1.00 (screenshot each) | Where do the zigzag corners land on the ruler? Are the handles, ticks and background really gone (just a blue line)? | PASS (measured) | `evidence/G-env2-17..22.png`. **Hidden extras work**: zero-size transparent handles, transparent scale and background leave only the line (the skin loads). **Horizontal**: in the 1,216 px box the line starts 72 px in; each timed stage is **209 px x its value** (0.25: 52, 0.5: 105, 0.61: 127, 1.0: 209), linear; the **sustain stretch is a fixed ~210 px** flat segment the plugin cannot shorten. **Vertical**: levels 0.1 / 0.9 drew at about 42 % / 61 % of this 200 px box, but at about 13 % / 86 % of the 170 px lower box, so the vertical scale depends on the box (size or style) |
| G2 | BEND from 0 to 1 (screenshot at both ends) | Do the lines in the lower two envelopes bend? Do shape handles appear? | PARTIAL | With `curveType` Exponential and `forceHideShapeHandles` false, **ADSR bends**: BEND 0 = straight, 0.5–1 = increasingly curved attack / decay / release, with shape-handle dots shown. **Pitch never bends** (straight at 0 and 1) |

### Experiment B conclusion

Native Envelopes draw smooth anti-aliased vector lines and follow plugin values smoothly (E1, E2), and they
can be stripped down to a bare line (G1). But they cannot draw an EQ curve: Pitch (the only type with free
levels) has straight segments only and a fixed-width flat sustain stretch, and its vertical scale changes with
the box; ADSR bends (G2) but its shape is fixed (up to the top, down to sustain, flat, down to zero). **The
film-strip line (K7d) remains the way to draw EQ curves.**
