# EQ Artist

An EQ for the Akai MPC standalone (MPC OS 3.10.1+, insert effect) that you draw:

1. Drag your finger across the graph, over a live spectrum of the track. The sound follows as you draw:
   the drawing is continuously fitted to up to 8 real parametric bands (bell, shelf, cut, notch, tilt),
   using zero-latency filters that follow their analog shapes, so high bands keep their width instead of
   narrowing towards 20 kHz.
2. **UNDO** takes back a stroke; **CLEAR** goes back to flat.
3. Fine-tune each band's Freq / Gain / Q / Type / Slope on the BANDS pages or with the Q-Links; the drawing
   follows.

**Status: 0.3.2.** Tested in the desktop simulator, and the drawing surface on an MPC Live III.
Controls: [docs/CONTROLS.md](docs/CONTROLS.md). How the drawing surface was chosen (the pen won):
[docs/SPIKE.md](docs/SPIKE.md).

## Layout

| Path | What |
|---|---|
| `core/include/slowops/eqartist/` | Header-only DSP, standard library only (no plugin SDK): `Filters` (band shapes, analog-matched zero-latency biquads), `EqEngine` (8 bands, glides, click-free crossfades), `CurveFit` (drawing → bands), `FitWorker` (runs the fit off the audio thread), `Spectrum` + `Fft` (analyser). |
| `plugin/` | MPC Plugin Builder SDK project: `plugin.json`, `skins/`, and `src/EqArtist.cpp` (the adapter). |
| `scripts/` | `build.sh`, `test.sh`, `gen_skins.py` (TUI.json + Q-Links.json), `make_skin_art.py` (PNGs without text, standard library only), `make_text_art.py` (button labels and graph labels, needs Pillow), `skin_layout.py` (shared layout and palette). |
| `assets/fonts/` | Titillium Web Bold (SIL OFL 1.1, see `OFL.txt`), drawn into the skin images by `make_text_art.py`. `plugin/skins/knob_uni.png` is the SDK's UI-kit knob (MIT), rendered in purple. |
| `tests/core_tests.cpp` | DSP unit tests: filters against their analog shapes, cut slopes, engine stability, curve fitting, analyser. |
| `tests/scenario_host.cpp` | Loads the real plugin binary through the VST2 ABI, draws with the pen and measures that the audio follows the drawing; also Undo, Clear, Bypass, knob edits, clicks and the displays. Writes `plugin/build/draw-demo.wav`. |
| `docs/CONTROLS.md` | Every control, the Q-Link map and the parameter list. |
| `docs/SPIKE.md` | The Phase 0 drawing-surface test and its result. |
| `third_party/mpc-plugin-builder-sdk` | Official SDK as a git submodule, pinned to `febd2ca` (2026-11-07). |

## Build and test

```sh
git submodule update --init
./scripts/test.sh        # build (native + arm32 + arm64), core tests, host_sim, scenario host
```

On the first run, `build.py` installs Zig into `third_party/mpc-plugin-builder-sdk/.sdk-tools/` (about 50 MB).
The output folder is `plugin/build/Slow Ops - VST - EQ Artist/`. Copy the whole folder into
`VST2 Plugins/` at the root of a drive the MPC can see.

The skin images are committed. After changing `scripts/skin_layout.py`, run `python3 scripts/make_skin_art.py`, then
`scripts/make_text_art.py` with a Python that has Pillow (it adds the text to the images).

## Licence

MIT. The SDK is MIT-licensed by inMusic.
