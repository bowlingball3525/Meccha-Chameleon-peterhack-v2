# Camouflage (MecchaCamouflage mesh_first_paint)

peterhack loads the native camo **bridge** in-process and talks to it over TCP (`BridgeStartV1` + hello handshake, or legacy port **47654**).

1. **Load** `meccha-xenos-bridge.dll` into the game process.
2. Bridge listens on **127.0.0.1** (dynamic port from `BridgeStartV1`, or 47654 for legacy builds).
3. peterhack sends **`paint_full_route`** with mesh-first-paint JSON.

## Layout (deploy folder)

```
Desktop/peterhack/
  peterhack.dll
  peterhack-loader.exe
  manifest.json
  bridge/
    meccha-xenos-bridge.dll
    mesh-profiles/
      paintman.mesh-profile-v2.json
      paintman_cube.mesh-profile-v2.json
```

Repo source:

```
runtime/          # Bridge C++ source + build script
peterhack/        # CamoManager, CamoSettings, menu tab
```

## Build

From repo root (recommended):

```bat
build.bat
```

Bridge only:

```powershell
powershell -File runtime\scripts\build-camo-bridge.ps1 -DeployDir "$env:USERPROFILE\Desktop\peterhack"
```

Optional copy helper:

```powershell
powershell -File scripts\setup-camo-bridge.ps1
```

## In-game

1. Open menu (**INSERT** or **F10**) → **Camouflage** tab (bridge preloads on first menu open).
2. Enable **Enable camo hotkeys** → **Save camo settings** (off by default).
3. In a match, wait 3 seconds after spawn for hotkeys to arm (see console).
4. Hotkeys (menu closed, game focused):

| Key | Action |
|-----|--------|
| F1 | Start full paint |
| F2 | Preview |
| F3 | UnPreview |
| F4 | Stop |

Settings persist to `C:\peterhack\camo.cfg`.

## Rebuilding after bridge source updates

Replace `runtime/src/bridge.cpp` from upstream MecchaCamouflage if needed, then:

```bat
build.bat
```
