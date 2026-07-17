## peterhack v2.7.2

Download **`peterhack-v2.7.2-win64.zip`**, extract anywhere (e.g. Desktop), then run:

`peterhack-loader.exe --local --wait`

**Important:** fully exit the game before updating — the bridge DLL stays loaded until the process exits.

---

## New since v2.7.1

### Game update (July 17, 2026 build)
- Bridge now recognizes **`PenguinHotel-Win64-Shipping.exe`** (July 17 PE identity) alongside the older Steam build
- Updated packed-paint and no-resend route RVAs for the current shipping binary
- Fixes camo failing with `main_module_build_identity_mismatch` after the game rename/update

### Camouflage — in-game quality
- **Auto material ON by default** — reads metallic/roughness from the target's existing paint
- **All regions Paint by default** (no front Fill) for stroke-based camo like the in-game tool
- Bridge initializes paint textures before material detection; auto material applies to fill strokes too
- **Pacing floor lowered to 25 ms** (slider 25–500; values under 50 ms are faster but riskier online)
- **"Reset to in-game quality defaults"** button in the Camo tab

### ESP
- **Outline controls** — thickness, color/alpha, per-element toggles (box, lines, name, role, distance, skeleton)
- Fixed **ghosting/stacked text** at high outline thickness (concentric 1 px rings for text)

### Magnet
- Menu checkbox is the master enable; hotkey toggles **active** state only while enabled
- Unchecking in menu no longer gets overridden by a stale hotkey edge

### SDK
- Refreshed SDK dump for SDK **5.6.1-44394996**

---

## Build info
- Tag: `v2.7.2`
- Branch: `github-main`
- Built: Release | x64
- Output: `peterhack.dll`, `peterhack-loader.exe`, `bridge/peterhack-bridge.dll`
