# peterhack v2.7.1

## New since v2.7.0

### Rebrand
- **`peterhack-bridge.dll`** replaces `meccha-xenos-bridge.dll`
- Bridge start magic **`PHK1`** — update DLL + bridge together after any bridge change

### Combat
- Silent aim reliability fixes (function pointer refresh, fire pre-arm, kill fallback)
- Combat tab: FOV / target settings appear under Silent Aim toggle

### Camouflage
- Partial camo on extreme emote / wall-clip poses (unsafe samples skipped instead of hard block)

## Install / Update
1. Extract `peterhack-v2.7.1-win64.zip` (or let the loader auto-update).
2. Run `peterhack-loader.exe --local --wait`.
3. Launch the game (or wait for it with `--wait`).

Close the game completely before replacing files.

## Package contents
| File | Purpose |
|------|---------|
| `peterhack-loader.exe` | Inject + optional auto-update |
| `peterhack.dll` | Overlay, ESP, tools, camo client |
| `manifest.json` | Loader file list / version |
| `bridge/peterhack-bridge.dll` | Mesh-first paint bridge |
| `bridge/mesh-profiles/*.json` | Camo mesh profiles |
| `fonts/fa-solid-900.ttf` | Menu icons |

See [USAGE.md](USAGE.md) and [CAMOUFLAGE.md](CAMOUFLAGE.md) for in-game controls.
