# Controls

Parameter ids become permanent at 1.0: saved projects find parameters by id, so from then on new
parameters are only appended. Indices are the order the MPC sees (`plugin/build/params.json`).

## EQ tab

| Control | What it does |
|---|---|
| *the graph* | Drag on it to draw. The sound follows your finger at once: while you draw you hear the live EQ, 32 bells solved straight from the drawing. Meanwhile the drawing is fitted to up to *Max Bands* real bands; when the stroke ends (0.15 s without movement) and the fit has caught up, the sound crossfades to the bands (30 ms, no click). Columns your finger skips are filled in. |
| *bright dots* | The EQ you hear (the live EQ while drawing, then the fitted bands). |
| *small rings* | Your drawing. A dot inside a ring means the EQ is where you drew it. |
| *status, top right* | How many bands the fit used and how far they are from the drawing (RMS dB), or **BYPASSED**. |
| **CLEAR** | Flat drawing, no bands. |
| **UNDO** | Takes back the last stroke or Clear (drawing and sound together). Press again to redo. |
| **BYPASS** | Lit when bypassed. Ramped (20 ms). |
| **ANALYZER** | Lit when the spectrum is shown behind the curve. |
| **Output** (knob) | −24 … +12 dB. |
| **Max Bands** (knob) | 1 … 8: the most bands the fit may use. Fewer bands give a simpler, broader EQ. |
| **Analyzer: PRE / POST** | The spectrum of the input (before the EQ) or the output (after it). |
| **Speed: SLOW / MED / FAST** | How fast the spectrum falls: 20 / 40 / 80 dB/s. |

Turning a band's Q-Link (or automating it) changes the EQ directly. Once the knob has been
still for 0.3 s, the drawing is redrawn from the EQ you hear, so the next stroke edits that curve.

The graph runs from 20 Hz to 20 kHz (log scale) and ±18 dB. It shows 32 columns of about 1/3 octave each.
The spectrum reads the total level in each column: a full-scale sine reads 0 dB, and pink noise reads flat.

## PARA tab (the first tab; UI test: its bands are silent)

An interactive parametric EQ. **For now its bands do not change the sound** (they are separate test bands;
wiring them to the EQ comes next).

| Control | What it does |
|---|---|
| *band nodes* | Touch near a node (about 44 px) to grab and select it, then drag: left/right is frequency, up/down is gain. The node keeps its offset from your finger, so it doesn't jump. Cuts and notches only move left/right. A pause of 0.25 s ends a touch (the MPC sends no "lift"). Touching empty space does nothing |
| *white curve* | The **selected band's own curve**, over a flat fill in the band's colour (it fades out where the band is flat at 0 dB). With **NONE** selected: the combined curve |
| *grey line* | The combined response of all shown bands, behind |
| **DOTS / LINE** (top) | STYLE: both curves as dots (the default) or as smooth lines |
| **ANALYZER** (top) | The spectrum behind the curves on / off (the same switch as DRAW's ANALYZER) |
| **NONE**, **1–8** (top) | Select no band (only the combined curve) or a band (each in its colour) |
| *status* | The selected band: type, frequency, gain, Q (or slope for cuts) |
| **BANDS − / +** | **+** adds a band (flat, at its default frequency) and selects it; **−** removes the selected band (the bands above it move down a place) and selects none. 1–8 bands, starting at 2 |
| **TYPE** | BELL, LO SH, HI SH, LO CUT, HI CUT, NOTCH for the selected band |
| **Q**, **SLOPE** (knobs) | The selected band's Q, and its slope (cuts only) |

Q-Links on PARA: Q-Link 13 to 16 = the selected band's Freq, Gain, Q, Slope.

## The bands (DRAW)

There is one page. The fit chooses each band's type, frequency, gain, Q and slope (up to *Max Bands*
of them); the Q-Links turn the Freq and Gain of bands 1–8, and every band parameter can be automated.

| Type | Shape |
|---|---|
| Bell | Boost or cut around Freq |
| Low Shelf / High Shelf | Boost or cut below / above Freq |
| Low Cut / High Cut | Butterworth roll-off, 6–48 dB/oct |
| Notch | Deep cut at Freq |
| Band Pass | Keeps only around Freq |
| Tilt | −Gain/2 below Freq, +Gain/2 above |

All filters have zero latency. Their response follows the analog shape: within 0.3 dB up to about 11 kHz
for bands up to 9 kHz, so a high bell keeps its width instead of narrowing towards 20 kHz.

## Q-Links

| Where | Q-Links |
|---|---|
| EQ page | Freq and Gain of bands 1–8, two bands per row (Q-Link 13 = band 1 Freq) |
| Program mode | Freq and Gain of bands 1–8, in order (Q-Link 1 = band 1 Freq) |

Drawing, the buttons and the read-only displays are never on a knob.

## Parameter list

| Index | Id | Name | Automatable |
|---|---|---|---|
| 0 | `output` | Output | yes |
| 1–2 | `clear`, `undo` | Clear, Undo (self-resetting) | yes |
| 3 | `maxbands` | Max Bands | yes |
| 4 | `analyzer` | Analyzer (on/off) | yes |
| 5 | `analyze` | Analyze (Pre/Post) | yes |
| 6 | `speed` | Speed | yes |
| 7 | `bypass` | Bypass | yes |
| 8–9 | `penx`, `peny` | Pen X / Pen Y (the pen pad) | no |
| 10 | `fitinfo` | Fit (status text) | no |
| 11–58 | `b{n}on`, `b{n}type`, `b{n}freq`, `b{n}gain`, `b{n}q`, `b{n}slope` | B*n* On … Slope, n = 1…8 | yes |
| 59–90 | `draw1` … `draw32` | Draw 1 … 32 (the drawing, saved with the project) | no |
| 91–122 | `spec1` … `spec32` | Spectrum display | no |
| 123–154 | `resp1` … `resp32` | EQ response display | no |
