# Curve UI Test

An isolated diagnostic plugin for Experiment A of `MPC_Curve_UI_Research_and_Test_Plan.md`: does the MPC's
native `XYPad` drive plugin parameters reliably, and how does it behave? Audio passes through unchanged
(Level only). This plugin is separate from EQ Artist: its own name ("Curve UI Test"), uid `SoCt` and folder
(`Slow Ops - VST - Curve UI Test`). Installing it never touches EQ Artist.

Tabs: **FULL** (one big pad, edge reach / rate / release / Q-Links / Auto Move), **SPLIT** (two pads side
by side), **STACK** (two pads in one rectangle), **SAFE** (plain sliders, the known-good fallback).

## Build and test

```sh
./scripts/build.sh      # native + arm32 + arm64, validation, SDK host simulator, scenario host
```

The skin images are committed. After changing `scripts/layout.py`, run `scripts/make_art.py` with a Python
that has Pillow, then `./scripts/build.sh`.

## Install

On the MPC: MPC OS 3.10.1+, Preferences → Enhancements (Beta) → Enable Enhancements and External Plugin
Support. Then either copy `build/Slow Ops - VST - Curve UI Test/` into `VST2 Plugins/` on a drive, or over
the network (developer mode):

```sh
python3 ../third_party/mpc-plugin-builder-sdk/tools/mpc_remote.py <mpc-ip> add . --drive <n>
```

Save your project before restarting the MPC app to rescan. Run the checklist in `test-results/live3.md`.

## Safety

No root, firmware changes, raw input devices or display overlays. The only device access is the SDK's
`mpc_remote.py`, which writes only inside the `VST2 Plugins` folder.
