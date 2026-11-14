# Sources, versions and pinned commits

Recorded 2026-11-14 for Experiment A (native XYPad).

| Item | Value |
|---|---|
| MPC Plugin Builder SDK | https://github.com/inmusic-software/mpc-plugin-builder-sdk, commit `febd2ca5bd9eb16c4a412837196e145566bc981b` (2026-11-07, "Add plugin format to plugin.json"), the submodule at `../third_party/mpc-plugin-builder-sdk` |
| Widget reference used | `docs/ui-reference.md` §6.14 XYPad (marked "less tested"), §4 components, §7.4 visibility |
| Device | Akai MPC Live III, MPC OS 3.10.1.12, firmware 9a27d579, Cortex-A55 ×8 aarch64, Linux 6.18.44-imb-rt6 (`tools/mpc_remote.py info`) |
| External plugin support | On (EQ Artist and Slow Ops Looper load on this device) |
| Deployment | `tools/mpc_remote.py add` over the network (the SDK's official route, developer mode), drive "Live3Drive" |
| Build host | macOS 26.5.1, Apple clang 21.0.0; arm32/arm64 cross builds by the SDK's bundled Zig |
| MPC XL | Not available for this round |

## Not used

- Community project https://github.com/sunskiefer/Pseudo-X-Y-pad (DRM/KMS overlay, raw `evdev` touch input,
  reported on Akai Force, MPC OS 3.9.1). Not read into this build and not run on the device: it needs
  privileged access the plan excludes without explicit permission (research plan §9).
