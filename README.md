# EQ Artist

An EQ for the Akai MPC standalone (MPC OS 3.10.1+, insert effect) that you draw:

1. Tap **Draw** and sketch the curve you want over a live spectrum of the track.
2. Tap **Apply**: the sketch is fitted to up to 8 real parametric bands (bell, shelf, cut, notch, tilt),
   using zero-latency filters whose response matches their analog shapes all the way up to 20 kHz.
3. Fine-tune each band's Freq / Gain / Q / Type / Slope on its own page or with the Q-Links.

**Status: Phase 0 spike.** The current build tests the drawing surface and the spectrum display on the
hardware. Audio passes through unchanged. See [docs/SPIKE.md](docs/SPIKE.md) for the checklist.

## Layout

| Path | What |
|---|---|
| `core/include/slowops/eqartist/` | Header-only DSP, standard library only (no plugin SDK): `Fft`, `Spectrum`. |
| `plugin/` | MPC Plugin Builder SDK project: `plugin.json`, `skins/`, and `src/EqArtist.cpp` (the adapter). |
| `scripts/` | `build.sh`, `test.sh`, `gen_skins.py` (TUI.json + Q-Links.json), `make_skin_art.py` (PNGs, standard library only), `skin_layout.py` (shared layout and palette). |
| `tests/core_tests.cpp` | DSP unit tests. |
| `tests/scenario_host.cpp` | Loads the real plugin binary through the VST2 ABI and checks pen drawing, swipe counting, the spectrum display and the display update rate. |
| `docs/SPIKE.md` | Hardware checklist for the drawing surface. |
| `third_party/mpc-plugin-builder-sdk` | Official SDK as a git submodule, pinned to `febd2ca` (2026-11-07). |

## Build and test

```sh
git submodule update --init
./scripts/test.sh        # build (native + arm32 + arm64), core tests, host_sim, scenario host
```

On the first run, `build.py` installs Zig into `third_party/mpc-plugin-builder-sdk/.sdk-tools/` (about 50 MB).
The output folder is `plugin/build/Slow Ops - VST - EQ Artist/`. Copy the whole folder into
`VST2 Plugins/` at the root of a drive the MPC can see.

The skin images are committed. After changing `scripts/skin_layout.py`, run `python3 scripts/make_skin_art.py`.

## Licence

MIT. The SDK is MIT-licensed by inMusic.
