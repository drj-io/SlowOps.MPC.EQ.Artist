# Phase 0 hardware spike: the drawing surface

The MPC never opens a plugin's own editor. Its screen is a JSON skin of widgets bound to parameters,
so there is no canvas to draw on. This spike build tests three ways of drawing a curve, and how
fast the screen can redraw a live spectrum. The results decide what the real EQ page uses.

Audio passes straight through. Only **Output** (Q-Link 13) touches the sound.

## Install

1. `./scripts/test.sh` (or `./scripts/build.sh`).
2. Copy `plugin/build/Slow Ops - VST - EQ Artist/` into `VST2 Plugins/` at the root of a drive the MPC
   can see. Restart the MPC.
3. Add **EQ Artist** as an insert on an audio track with music playing through it.

## The three tabs

| Tab | What's on it |
|---|---|
| **PEN** (F1) | Spectrum bars, the drawn curve (one dot per column), and one invisible touch pad over the whole graph. Drag a finger across the graph: the plugin paints the column under your finger and fills in any columns your finger skipped. |
| **COLUMNS** (F2) | Spectrum bars and 32 thin vertical sliders (the dots). **Swipe Cols** shows how many different columns your last gesture changed. |
| **OVERLAP** (F3) | The same sliders with the touch pad on top of them, to see which one gets the touch. |

The bottom row on every tab has four controls:
- **Clear** flattens the curve.
- **Source** switches between Input (real spectrum) and Sweep (a bump that crosses every column every 4 s, no audio needed).
- **Disp Rate** (30 / 15 / 60 Hz) sets how often the spectrum is redrawn.
- **Flip Y** inverts the pen's up/down direction.

## Checklist

Record pass / fail and a note for each. Tested on: _MPC model, MPC OS version, date_.

### A. Pen (PEN tab)
| # | Check | Result |
|---|---|---|
| A1 | Touching the graph moves the ring puck to your finger. | |
| A2 | Dragging paints the curve under your finger. Up is boost and down is cut (if it's upside down, try **Flip Y** and note it). | |
| A3 | A fast left-to-right swipe paints a continuous curve with no gaps. | |
| A4 | **PEN /S** during a steady drag: write down the number. Under about 15 means the pen will feel laggy. | |
| A5 | **X** and **Y** follow the finger the whole way to all four edges of the graph. | |
| A6 | Lifting and touching again elsewhere starts a new stroke, with no line jumping back to the old one. | |
| A7 | Does the pad leave the screen's focus on itself, so the data wheel / Q-Links change Pen X or Y? Is that harmful? | |

### B. Columns (COLUMNS tab)
| # | Check | Result |
|---|---|---|
| B1 | Dragging a single column's dot moves it up and down. | |
| B2 | **One swipe across several columns:** what does **Swipe Cols** read? It reads 1 if only the first column grabbed the touch, or more if the touch passed between widgets. | |
| B3 | Tapping a column (not dragging): does the dot jump to the finger, or does it only move relative to the drag? | |
| B4 | The data wheel edits the focused column. | |

### C. Overlap (OVERLAP tab)
| # | Check | Result |
|---|---|---|
| C1 | Touch where a dot is: does the pad (PEN /S counts up) or the slider (Swipe Cols changes) get it? | |
| C2 | Touch empty space in a column: same question. | |

### D. Redraw
| # | Check | Result |
|---|---|---|
| D1 | **Source = Sweep, Disp Rate 30 Hz:** does the bump glide across smoothly? Note any stutter or lag. | |
| D2 | Same at **15 Hz** and **60 Hz**. Which rate is the highest that stays smooth? | |
| D3 | While the sweep runs, is drawing (A2 / B1) still responsive? | |
| D4 | **Source = Input** with music playing: does the spectrum look right (bass on the left, hats on the right, and falling smoothly)? | |
| D5 | MPC CPU meter with the plugin running, Disp Rate 30 Hz. | |
| D6 | Pen on a 1 kHz-heavy sound: the curve dots stay drawn on top of the spectrum, not hidden behind it. | |

### E. Project
| # | Check | Result |
|---|---|---|
| E1 | Draw a curve, save the project, reload it: the curve comes back unchanged and no stray stroke is painted. | |

## What the answers decide

- **A1–A3 pass:** the pen (an invisible pad over the graph) is the drawing surface. The column sliders
  are only needed for fine edits, or not at all.
- **A fails, B2 > 1:** swiping across the sliders draws, so the sliders are the drawing surface.
- **A fails, B2 = 1:** draw one column at a time; consider fewer, wider columns (24) to make that faster.
- **D:** the display rate and column count of the release build (scale down to 24 columns or 15 Hz if
  30 Hz stutters).

## Results

_Not tested on hardware yet._

## Desktop results (scenario host, 2026-11-14)

- Pen strokes paint continuous curves, joining skipped columns. A pause over 0.15 s starts a new stroke.
- With Source = Sweep, the plugin sends the host about 240 / 140 / 400 spectrum updates per second at
  30 / 15 / 60 Hz (each column is only sent when its bar height changes).
- A project state round trip restores every parameter, and loading a project never paints a stroke.
