# Controls

Parameter ids are permanent: saved projects find parameters by id, so new parameters are only ever
appended. Indices are the order the MPC sees (`plugin/build/params.json`).

## EQ tab

| Control | What it does |
|---|---|
| **DRAW** | Draw mode. The curve becomes your drawing, which starts from the EQ you hear, and the pen is live: drag on the graph to paint it. Columns your finger skips are filled in, and lifting for 0.15 s starts a new stroke. |
| **APPLY** | Fits the drawing to up to *Max Bands* real bands and switches them in (30 ms crossfade, no click). Draw turns off so you see and hear the result. The status at the top right shows the band count and how far the bands are from the drawing (RMS dB). |
| **CLEAR** | Flattens the drawing. Apply after Clear removes all bands. |
| **UNDO** | Back to the bands before the last Apply. Press again to redo. |
| **Output** | −24 … +12 dB. |
| **Max Bands** | 1 … 8: the most bands Apply may use. Fewer bands give a simpler, broader EQ. |
| **Analyzer** | Off / Pre (input) / Post (output) spectrum behind the curve. |
| **Speed** | How fast the spectrum falls: Slow 12, Medium 24, Fast 48 dB/s. |
| **Bypass** | Ramped (20 ms). |

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
| 1 | `draw` | Draw | yes |
| 2–4 | `apply`, `clear`, `undo` | Apply, Clear, Undo (self-resetting) | yes |
| 5 | `maxbands` | Max Bands | yes |
| 6 | `analyzer` | Analyzer | yes |
| 7 | `speed` | Speed | yes |
| 8 | `bypass` | Bypass | yes |
| 9–10 | `penx`, `peny` | Pen X / Pen Y (the pen pad) | no |
| 11 | `fitinfo` | Fit (status text) | no |
| 12–59 | `b{n}on`, `b{n}type`, `b{n}freq`, `b{n}gain`, `b{n}q`, `b{n}slope` | B*n* On … Slope, n = 1…8 | yes |
| 60–91 | `draw1` … `draw32` | Draw 1 … 32 (the drawing, saved with the project) | no |
| 92–123 | `spec1` … `spec32` | Spectrum display | no |
| 124–155 | `resp1` … `resp32` | EQ response display | no |
