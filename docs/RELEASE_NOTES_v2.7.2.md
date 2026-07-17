# peterhack v2.7.2

## New since v2.7.1

### Game update (July 17 build)
- July 17 `PenguinHotel-Win64-Shipping.exe` build identity + camo route RVAs
- Resolves `main_module_build_identity_mismatch` after the game executable rename

### Camouflage
- In-game quality defaults: auto material, all Paint regions, roughness 0.65
- Paint init before dominant material detection; auto material on fill strokes
- Pacing minimum 25 ms

### ESP / Magnet
- ESP outline thickness, color, per-element mask; thick-outline ghost fix
- Magnet master enable vs hotkey active toggle split

### SDK
- SDK 5.6.1-44394996 refresh

## Install

1. Extract `peterhack-v2.7.2-win64.zip`.
2. Run `peterhack-loader.exe --local --wait`.
3. Update **both** `peterhack.dll` and `bridge/peterhack-bridge.dll` together.
