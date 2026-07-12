# peterhack — usage guide

Internal overlay/tools for **Meccha Chameleon** (UE5, DirectX 12). Game process: `PenguinHotel-Win64-Shipping.exe`.

> **Disclaimer:** Educational/research use only. The authors are not responsible for bans or misuse. Do not use this to ruin other players' experiences.

---

## Quick start (recommended)

### 1. Build (developers)

Requirements:

- Windows 10/11 x64
- Visual Studio 2022+ with **Desktop development with C++** and **Windows 10/11 SDK**
- MSVC toolset **v143** or newer

From the repo root:

```bat
build.bat
```

Output lands in **`%USERPROFILE%\Desktop\peterhack\`**:

| File | Purpose |
|------|---------|
| `peterhack-loader.exe` | Downloads/copies DLLs and injects into the game |
| `peterhack.dll` | Main overlay (menu, ESP, tools, camo client) |
| `manifest.json` | File list for the loader |
| `bridge\meccha-xenos-bridge.dll` | Camouflage TCP bridge |
| `bridge\mesh-profiles\*.json` | Mesh paint profiles |

Close the game before building if you get `LNK1104: cannot open peterhack.dll` (file locked).

### 2. Run the game + inject

**Easiest path** — loader waits for the game and uses local files:

```bat
"%USERPROFILE%\Desktop\peterhack\peterhack-loader.exe" --local --wait
```

1. Start this **before** or **after** launching Meccha Chameleon (with `--wait`, it waits up to 5 minutes).
2. A console window opens with peterhack logs.
3. Focus the game window.

**Manual inject:** If you already use Xenos or another injector, copy the whole `Desktop\peterhack` folder somewhere stable and inject **`peterhack.dll`** into `PenguinHotel-Win64-Shipping.exe`. The loader is optional but handles bridge file layout for you.

### 3. Open the menu

| Key | Action |
|-----|--------|
| **INSERT** | Open / close menu |
| **END** | Unload peterhack (eject DLL) |

Settings save to `C:\peterhack\settings.ini`. Camo settings save to `C:\peterhack\camo.cfg`.

---

## Loader options

```
peterhack-loader.exe [options]

  --local         Use files in the deploy folder (no HTTP download)
  --inject-only   Skip copy step; files must already be in deploy folder
  --wait          Wait up to 5 minutes for the game process
  --manifest <path>   Manifest JSON (default: manifest.json next to loader)
  --deploy <dir>  Deploy folder (default: %USERPROFILE%\Desktop\peterhack)
  --help          Show help
```

**Remote updates:** Set `"baseUrl"` in `manifest.json` to an `https://` URL hosting the same file paths. Omit `--local` to download on each run.

---

## Menu overview

### Visuals tab

- **ESP overlay** — master toggle for the world scan / on-screen ESP (can turn off for performance).
- Box, skeleton, lines, distance, names, roles, decoys, enemy-only filter.
- Color pickers per role (survivor, hunter, decoy).

### Tools tab

**Survivors**

- Anti Detection — clears overlap capsules used for “too buried” detection.
- No Decoy Cooldown — removes clone/decoy timer locally.
- Set Clone Amount — slider 0–99 + server RPC for max decoy count.

**Hunters**

- No Gun Cooldown / Infinite Bullets
- Magnet — pull survivors in view (toggle default **G**, rebind in menu)
- Kill All / Kill selected survivor (host-oriented)
- Return to lobby button

### Camouflage tab

Mesh-first-paint camo via in-process bridge. See [CAMOUFLAGE.md](CAMOUFLAGE.md) for build/layout details.

1. Open menu → **Camouflage**.
2. Check **Enable camo hotkeys** and **Save camo settings** (hotkeys are off by default).
3. Enter a match; console prints: `Match entered — camo hotkeys armed in 3s`.
4. After 3 seconds in-match (game focused, menu closed):

| Key | Action |
|-----|--------|
| **F10** | Start full paint |
| **F2** | Preview |
| **F3** | UnPreview |
| **F4** | Stop (works during active paint) |

Or use the **Start / Preview / UnPreview / Stop** buttons in the tab.

Bridge loads automatically when you open the menu or when hotkeys arm in a match.

---

## Runtime folders

| Path | Contents |
|------|----------|
| `%USERPROFILE%\Desktop\peterhack\` | Built binaries (deploy output) |
| `C:\peterhack\settings.ini` | Menu / ESP / tools settings |
| `C:\peterhack\camo.cfg` | Camouflage tuning + hotkeys |

peterhack resolves the bridge from `bridge\` next to `peterhack.dll` (the loader deploys this layout).

---

## Building from Visual Studio

Open `peterhack.slnx`, configuration **Release | x64**, build projects:

- **peterhack** → `peterhack.dll`
- **peterhack-loader** → `peterhack-loader.exe`

Or use `build.bat`, which also compiles the camo bridge via `runtime\scripts\build-camo-bridge.ps1`.

---

## Troubleshooting

| Problem | What to try |
|---------|-------------|
| Build fails: cannot open `peterhack.dll` | Close game and loader; rebuild |
| No menu | Confirm injection (console should show DX12 hook messages); focus game window |
| Tools/camo do nothing | Enable toggles in menu; for camo enable hotkeys + wait 3s in match |
| Bridge error on Camo tab | Run `build.bat`; ensure `bridge\meccha-xenos-bridge.dll` exists next to loader |
| Game crash | Unload with **END**, update to latest build; check console for `[ProcessEvent] Init fault` |
| Console spam | Fixed in recent builds (single-line logging); rebuild if you see duplicates |

---

## Updating SDK / game version

When the game updates:

1. Run [Dumper-7](https://github.com/Encryqed/Dumper-7) on the new executable.
2. Replace `peterhack/SDK/` with the new dump (or sync offsets).
3. Rebuild with `build.bat`.
4. Test in a private lobby first.

Current target version: **2.6.0** (see README).

---

## Credits

- [Dumper-7](https://github.com/Encryqed/Dumper-7) — SDK generation  
- [Unreal-Internal-Base](https://github.com/GLX-ILLUSION/Unreal-Internal-Base) — base patterns  
- [Dear ImGui](https://github.com/ocornut/imgui) — UI  
- [MecchaCamouflage](https://github.com/acentrist/MecchaCamouflage) — camo bridge reference  
