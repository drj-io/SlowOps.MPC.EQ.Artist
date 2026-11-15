# Controls

EQ Artist Dots and EQ Artist Lines have the same controls; they differ only in how curves are drawn (dots or
connected lines; Lines is experimental and its screen updates far more slowly). Both pages edit the same eight bands.

Parameter ids and order are permanent from 1.0: saved projects find parameters by id, so new parameters are
only appended. A project saved with one plugin does not load into the other.

## The data wheel

Every setting (TYPE, BANDS, Q, SLOPE, the band selection, BANDS on both pages, Analyzer PRE / POST, Speed) is one
focus stop: touch it and a purple ring shows around it, and the MPC's data wheel changes it. On BANDS the wheel sets
the band count; a band it switches on starts flat at its default spot, as with **+**.

## PARA (the first tab)

| Control | What it does |
|---|---|
| *band nodes* | Touch near a node (about 44 px) to grab and select it, then drag: left/right is frequency; up/down is gain (±18 dB) for a bell or shelf, the depth for a notch (the bottom is a full notch), and the resonance (Q) for a cut: on the 0 dB line it is a plain cut (Q 0.71), each 6 dB up doubles Q. The node keeps its offset from your finger, so it doesn't jump. A pause of 0.25 s ends a touch (the MPC sends no "lift"). Touching empty space does nothing |
| *white curve* | The **selected band's own curve**, over a flat fill in the band's colour. With **NONE** selected: the combined curve |
| *grey curve* | The EQ you hear: all bands combined |
| **CLEAR**, **UNDO**, **BYPASS**, **ANALYZER** (top left) | The same buttons as on DRAW, in the same place. UNDO also takes back a PARA edit: a drag, + / −, or a TYPE / Q / SLOPE change, counted as one step once you have stopped for 0.3 s |
| **NONE**, **1–8** (top right) | Select no band, or a band (each in its colour) |
| *status* (bottom, under PRE / POST) | The selected band: type, frequency, gain, Q (or slope for cuts) |
| **BANDS − / +** | **+** adds a flat band and selects it; **−** removes the selected band (the bands above it move down a place), or the last band when none is selected, and selects none. 1–8 bands, starting at 2 |
| **TYPE** (bottom left, 3 × 2) | BELL, LO SH, HI SH / NOTCH, LO CUT, HI CUT for the selected band |
| **Q** (knob) | The selected band's width (bell, shelves, notch) or its resonance (cuts) |
| **GAIN** / **SLOPE** (knob) | One knob that follows the band type: **GAIN** (±18 dB) for a bell or shelf, the depth for a notch (0 dB … −18 dB = **Full**), **SLOPE** (6–48 dB/oct) for a cut |
| **Analyzer: PRE / POST**, **Speed: SLOW / MED / FAST** | The same switches as on DRAW, in the same place: spectrum of the input or output, and how fast it falls |

While you drag a node, the node and that band's curve follow; the grey curve and the fill catch up when the
finger stops.

## DRAW

| Control | What it does |
|---|---|
| *the graph* | Drag on it to draw. The sound follows your finger at once (a 32-band live EQ solved straight from the drawing). Meanwhile the drawing is fitted to *BANDS* bands (any the curve doesn't need sit flat, at 0 dB); when the stroke ends (0.15 s without movement) and the fit has caught up, the sound crossfades to the bands (30 ms, no click). Columns your finger skips are filled in |
| *white curve* | The EQ you hear (the same curve as PARA's grey one) |
| *small rings* | Your drawing |
| *status* (top right) | How many bands there are and how far the last fit is from the drawing (RMS dB), or **BYPASSED** |
| **CLEAR** | A flat EQ: flat drawing, all bands flat (the count stays) |
| **UNDO** | Takes back the last stroke, Clear or PARA edit (drawing, bands and sound together). Press again to redo |
| **BYPASS** | Lit when bypassed. Ramped (20 ms) |
| **ANALYZER** | Lit when the spectrum is shown |
| **BANDS − n +** | 1 … 8: the band count, the same number as on PARA. Here **−** / **+** (or the data wheel) step it by one and refit your drawing with that many bands (one undo step) |
| **Analyzer: PRE / POST** | The spectrum of the input or of the output (the default) |
| **Speed: SLOW / MED / FAST** | How fast the spectrum falls: 20 / 40 / 80 dB/s |

## How the pages stay in step

- **A stroke on DRAW refits the whole curve** with the current band count. The bands and PARA's nodes change to
  match the drawing (bands the curve doesn't need sit flat), and no band stays selected.
- **The band count is one setting:** PARA's and DRAW's BANDS always show the same number.
- **An edit on PARA** (or a Q-Link, or automation) changes the sound directly. After 0.3 s without changes,
  the drawing on DRAW is redrawn from the EQ.

## The bands

Eight bands, each On, Type, Freq (20 Hz … 20 kHz), Gain (±18 dB), Q (0.1 … 18), Slope. Bands 1 … *Bands* are
on. All filters have zero latency and follow the analog shape: within 0.3 dB up to about 11 kHz for bands up
to 9 kHz.

| Type | Shape |
|---|---|
| Bell | Boost or cut around Freq |
| Low Shelf / High Shelf | Boost or cut below / above Freq |
| Low Cut / High Cut | Butterworth roll-off, 6–48 dB/oct; Q is its resonance |
| Notch | Cut at Freq; Gain is its depth (−18 dB or lower: a full, infinitely deep notch). Choosing NOTCH on PARA starts it full |

The graph runs from 20 Hz to 20 kHz (log scale) and ±18 dB. The spectrum has 32 columns of about 1/3 octave;
each reads the total level in its range, so pink noise reads flat.

## Q-Links

| Where | Q-Links |
|---|---|
| PARA | 13–16: the selected band's Freq, Gain, Q, Slope |
| DRAW | Freq and Gain of bands 1–8, two bands per row (13 = band 1 Freq) |
| Program mode | Freq and Gain of bands 1–8, in order |

## Parameters (in order)

| Parameters | Automatable |
|---|---|
| Output (−24 … +12 dB, no control on screen; the MPC nudges parameter 0 on every load), Clear, Undo, Max Bands (DRAW's BANDS), Analyzer, Analyze, Speed, Bypass | yes |
| Pen X, Pen Y, Fit (DRAW's pen and status) | no |
| Bands, Sel Band | yes |
| Para Pen X, Para Pen Y, Para Info, Sel Node X, Sel Node Y | no |
| Sel Type, Sel Freq, Sel Gain, Sel Q, Sel Slope (the selected band, for the controls and Q-Links) | yes |
| Add Band, Remove Band (self-resetting) | yes |
| B*n* On, Type, Freq, Gain, Q, Slope, n = 1 … 8 | yes |
| Node *n* X, Y; Draw 1–32 (the drawing, saved with the project); Spec 1–32 | no |
| Front, Curve, Fill columns (64 each in Dots, 76 each in Lines) | no |
| More Bands, Fewer Bands (DRAW's + / −, self-resetting) | yes |

Dots has 347 parameters, Lines 383.
