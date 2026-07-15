## peterhack v2.7.1

Download **`peterhack-v2.7.1-win64.zip`**, extract anywhere (e.g. Desktop), then run:

`peterhack-loader.exe --local --wait`

**Important:** fully exit the game before updating — the bridge DLL stays loaded until the process exits.

---

## New since v2.7.0

### Rebrand
- Camo bridge renamed to **`peterhack-bridge.dll`** (replaces `meccha-xenos-bridge.dll`)
- Bridge ABI magic updated to **`PHK1`** — always update **both** `peterhack.dll` and the bridge together
- Docs, loader manifest, and deploy layout updated for peterhack naming

### Combat (hunter)
- **Silent aim fixes** — live `UFunction` resolution after round reloads, LMB pre-arm, `KillPlayer` fallback when trace redirect misses, combat target lock no longer waits for ESP warmup
- **Combat menu** — shared target-selection (FOV / bone) moved below the Silent Aim toggle

### Camouflage
- **Extreme poses** — planner now **skips unsafe UV samples** instead of aborting the whole paint (helps emotes / wall-clip poses; may produce partial camo)
- Clearer error when no safe paint samples remain

### Stability (carried from v2.7.0 work)
- Nameplate stat RPC spam throttled
- Godmode death RPC blocks expanded + recovery repossess attempts
- ESP works during alive freecam; noclip uses Mover velocity instead of teleport
- Fill-mode camo defaults aligned with mesh-first paint pipeline

---

## Build info
- Tag: `v2.7.1`
- Branch: `github-main`
- Built: Release | x64
- Output: `peterhack.dll`, `peterhack-loader.exe`, `bridge/peterhack-bridge.dll`
