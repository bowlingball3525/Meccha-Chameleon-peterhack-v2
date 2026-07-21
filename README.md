# peterhack

Internal overlay and tools for **Meccha Chameleon** (DirectX 12). Targets game version **2.9.0**.

**Full setup and usage:** [docs/USAGE.md](docs/USAGE.md)  
**Camouflage / bridge details:** [docs/CAMOUFLAGE.md](docs/CAMOUFLAGE.md)  
**Latest release:** [RELEASE_v2.7.3_NOTES.md](RELEASE_v2.7.3_NOTES.md)

## Quick start

```bat
git clone <your-peterhack-repo-url>
cd peterhack
build.bat
"%USERPROFILE%\Desktop\peterhack\peterhack-loader.exe" --local --wait
```

In-game: **INSERT** = menu, **END** = unload.

## What you get

After `build.bat`, `%USERPROFILE%\Desktop\peterhack\` contains:

- `peterhack-loader.exe` — inject helper (local files + optional HTTP manifest)
- `peterhack.dll` — ESP, tools, ImGui menu, camo client
- `bridge\peterhack-bridge.dll` — mesh-first-paint camouflage bridge

## Features

- **ESP** — box, skeleton, lines, distance, names, roles, outlines, decoys
- **Tools** — survivor/hunter exploits, magnet, kill helpers, FOV, anti-kick, name change
- **Combat** — aimbot, silent aim, triggerbot, no recoil
- **Camouflage** — mesh-first-paint camo with packed PBR sync (F1–F4 hotkeys)
- **Loader** — single deploy folder, `--local --wait` workflow

## Build requirements

- Visual Studio 2022+ (MSVC v143+, Windows 10/11 SDK)
- Run `build.bat` from repo root (builds solution + camo bridge)

## Disclaimer

This project is for educational and research purposes only. The authors are not responsible for misuse, bans, or other consequences. Use at your own risk.

## Credits

[Dumper-7](https://github.com/Encryqed/Dumper-7) · [Unreal-Internal-Base](https://github.com/GLX-ILLUSION/Unreal-Internal-Base) · [imgui](https://github.com/ocornut/imgui)
