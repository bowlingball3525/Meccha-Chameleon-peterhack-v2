## peterhack v2.6.0

First packaged release for **Meccha Chameleon** game version **2.6.0** (DirectX 12).

### Download

Extract `peterhack-v2.6.0-win64.zip` to any folder, then run:

```bat
peterhack-loader.exe --local --wait
```

Launch **before or after** starting the game (`--wait` waits up to 5 minutes for `PenguinHotel-Win64-Shipping.exe`).

### Included files

- `peterhack-loader.exe` — copies/injects bundled DLLs
- `peterhack.dll` — menu, ESP, tools, camo client
- `manifest.json` — loader file manifest (`baseUrl: local`)
- `bridge/meccha-xenos-bridge.dll` — camouflage TCP bridge
- `bridge/mesh-profiles/*.json` — paint mesh profiles

### In-game

| Key | Action |
|-----|--------|
| INSERT | Open / close menu |
| END | Unload peterhack |

See [USAGE.md](https://github.com/bowlingball3525/MecchaChameleonpeterhackv2/blob/main/docs/USAGE.md) for ESP, Tools, and Camouflage (F10/F2/F3/F4) details.

### Build from source

Clone the repo and run `build.bat` (requires Visual Studio 2022+ with C++ desktop workload).

### Disclaimer

Educational/research use only. Use at your own risk.
