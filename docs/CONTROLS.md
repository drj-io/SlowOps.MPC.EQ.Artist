# Controls

Parameter ids become permanent at 1.0: saved projects find parameters by id, so from then on new
parameters are only appended. Indices are the order the MPC sees (`plugin/build/params.json`).

## EQ tab

| Control | What it does |
|---|---|
| *the graph* | Drag on it to draw. The sound follows as you draw: the drawing is refitted to up to *Max Bands* real bands whenever it changes, and each new set of bands is switched in with a 30 ms crossfade (no click). Columns your finger skips are filled in; lifting for 0.15 s ends the stroke. |
| *bright dots* | The EQ you hear. |
| *small rings* | Your drawing. A dot inside a ring means the EQ is where you drew it. |
| *status, top right* | How many bands the fit used and how far they are from the drawing (RMS dB), or **BYPASSED**. |
| **CLEAR** | Flat drawing, no bands. |
| **UNDO** | Takes back the last stroke or Clear (drawing and sound together). Press again to redo. |
| **BYPASS** | Lit when bypassed. Ramped (20 ms). |
| **ANALYZER** | Lit when the spectrum is shown behind the curve. |
| **Output** (knob) | −24 … +12 dB. |
| **Max Bands** (knob) | 1 … 8: the most bands the fit may use. Fewer bands give a simpler, broader EQ. |
| **Analyzer: PRE / POST** | The spectrum of the input (before the EQ) or the output (after it). |
| **Speed: SLOW / MED / FAST** | How fast the spectrum falls: 12 / 24 / 48 dB/s. |

Turning a band's knobs (BANDS pages, Q-Links, automation) changes the EQ directly. Once the knob has been
still for 0.3 s, the drawing is redrawn from the EQ you hear, so the next stroke edits that curve.

The graph runs from 20 Hz to 20 kHz (log scale) and ±18 dB. It shows 32 columns of about 1/3 octave each.
The spectrum reads the total level in each column: a full-scale sine reads 0 dB, and pink noise reads flat.

## BANDS 1-4, BANDS 5-8

One row per band: **On**, **Type**, **Freq** (20 Hz … 20 kHz), **Gain** (±24 dB), **Q** (0.1 … 18), **Slope**.

| Type | Shape | Uses |
|---|---|---|
| Bell | Boost or cut around Freq | Gain, Q |
| Low Shelf / High Shelf | Boost or cut below / above Freq | Gain, Q (0.71 = no overshoot) |
| Low Cut / High Cut | Butterworth roll-off | Slope 6–48 dB/oct, Q = resonance (0.71 = none) |
| Notch | Deep cut at Freq | Q |
| Band Pass | Keeps only around Freq | Q |
| Tilt | −Gain/2 below Freq, +Gain/2 above | Gain, Q |

All filters have zero latency. Their response follows the analog shape: within 0.3 dB up to about 11 kHz
for bands up to 9 kHz, so a high bell keeps its width instead of narrowing towards 20 kHz.

## Q-Links

| Where | Q-Links |
|---|---|
| EQ tab, Program mode | Freq and Gain of bands 1–8 (two bands per row on the EQ tab; 1–16 in order in Program mode) |
| BANDS tabs | One band per row: Freq, Gain, Q, Type |

Drawing, the triggers and the read-only displays are never on a knob.

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
