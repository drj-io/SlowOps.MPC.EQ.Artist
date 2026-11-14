# Compatibility matrix: Experiment A (native XYPad)

PASS / FAIL / PARTIAL / NOT TESTED. Details and evidence: `live3.md`, `xl.md`.

| Area | Test | Live III | XL |
|---|---|---|---|
| Deployment | Build, validator (0 errors), network install | PASS (Curve UI Test D1; prior P1) | NOT TESTED |
| Deployment | Skin parse, tabs visible | PASS (D1: all four tabs, captions in images) | NOT TESTED |
| Display | Value labels | PASS (prior, P2) | NOT TESTED |
| Display | Fixed-text labels | FAIL (prior, P3) | NOT TESTED |
| Input | XYPad drives two parameters | PASS (P4, D3; tap jumps to touch, A5 session 2) | NOT TESTED |
| Input | Edge reach, corners (A2, A3) | PASS: exactly 0.000–1.000 on both axes, puck centre reaches the edges (session 2, viewer) | NOT TESTED |
| Input | Update rate while dragging (A4) | PASS: finger about 45/s slow, peaks about 209/s fast (X and Y counted separately: about 22–105 points/s); viewer mouse 16–30/s | NOT TESTED |
| Input | Tap vs drag (A5), release (A6) | Tap PASS; release: **no signal** (updates just stop, same as holding still) | NOT TESTED |
| Mapping | Y direction; puck at the values' position (inverse mapping) | PASS: Y = 1 is the top; puck matches values (D2) | NOT TESTED |
| Parameters | Puck follows Q-Links (A7), plugin writes (A8) | Plugin writes PASS (A8); Q-Links NOT TESTED | NOT TESTED |
| Lifecycle | Tab switch, reopen, project reload, two instances (A9–A12) | Tab switch PASS (A9); the rest NOT TESTED | NOT TESTED |
| Input | Side-by-side pads (B1, B2), multi-touch (B3) | PASS: each pad its own; a drag stays with the pad it started in (B2). Multi-touch NOT TESTED | NOT TESTED |
| Input | Stacked pads (C1, C2) | **FAIL as separate nodes:** the top pad takes every touch, even on the lower pad's puck (C2) | NOT TESTED |
| Performance | Display updates ~230/s on the device | PASS (prior, P5) | NOT TESTED |
| Display | Film-strip curve, 64 columns at 0.5 dB, half strips (15,768 px) (K1) | PASS | NOT TESTED |
| Display | Film strips 31,536 px tall (K4) | PASS | NOT TESTED |
| Display | Connected line: 76 columns, 0.25 dB, 3,625-frame images 261,000 px tall, about 100 MB (K7c) | PASS: smooth, narrow Q 10 peaks reach the node | NOT TESTED |
| Display | Connected line: 152 columns, 0.125 dB, 8,959-frame images 645,048 px tall, about 124 MB; 1,152 widgets on one tab (K7d) | PASS: curved, peaks reach the node; tab opens with a small delay | NOT TESTED |
| Display | Native Path: unbound (handle `Invalid`) | FAIL: nothing drawn (L1) | NOT TESTED |
| Display | Native Path bound to a parameter | PARTIAL: smooth anti-aliased line, drawn at half the given coordinates; **the value has no effect** (L2): static geometry only | NOT TESTED |
| Display | Native Plotter | FAIL: nothing drawn (L3) | NOT TESTED |
| Display | Native Envelope (ADSR, Pitch, AD) bound to plugin parameters (E1, E2) | PASS: draws vector curves (AD smooth and curved), follows plugin values smoothly | NOT TESTED |
| Display | Envelope as an EQ-curve drawer (G1, G2) | FAIL: Pitch is straight-only with a fixed flat sustain stretch and a box-dependent vertical scale; ADSR bends but has a fixed envelope shape | NOT TESTED |
| Performance | Audio dropouts, CPU (X1, X2) | NOT TESTED | NOT TESTED |

Session 2 ran through the Remote Screen viewer (mouse as touch): see `live3.md`.

Simulator only (not evidence of touchscreen behaviour): `host_sim` PASS 20/20; `scenario_host` 15/15
(the diagnostics count, track, time and reset correctly; Auto Move is not counted as touches).
