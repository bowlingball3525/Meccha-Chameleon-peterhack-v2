# peterhack v2.6.4

> **⚠️ UNTESTED RELEASE** — this build compiles but has not been tested in-game yet. Use at your own risk and report issues.

## New

- **Controller keybind support** — new **Controller Binds** checkbox in the Tools tab. When enabled, you can rebind two actions to any XInput controller button (including LT/RT):
  - **Menu Button** (default **Back/View**) — opens/closes the menu, alongside INSERT/F10
  - **Magnet Button** (default **D-Pad Down**) — toggles magnet, alongside the keyboard magnet key

## Fixed

- **Camo failing on some maps** with `packed_radius_calibration_out_of_range` — the bridge rejected maps where the game inflates the character's bounds sphere, even though the calibrated value was correct there. The calibration now only rejects implausible (garbage-read) values and reports an `packed_mesh_radius_scale_within_expected_window` metadata flag for diagnostics.
- **Kill All Survivors** now only targets living real survivors — decoys/clones, hunters, self, and corpses are skipped — and kills are paced at 750 ms intervals instead of one per frame.

## Install

1. Extract `peterhack-v2.6.4-win64.zip`
2. Run `peterhack-loader.exe --local --wait`
3. Launch the game (or wait for it)

Settings from older versions reset once (the settings format changed for controller binds).
