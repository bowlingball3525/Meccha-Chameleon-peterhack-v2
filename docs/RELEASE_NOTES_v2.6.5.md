# peterhack v2.6.5

> **⚠️ UNTESTED RELEASE** — this build compiles and packages cleanly but has not been fully tested in-game. Keep v2.6.4 for rollback and please report issues.

## New since v2.6.4

### Added
- **Custom keybind recorder for Camouflage** — the Camo tab now lets you record a custom bind for each action (Start, Preview, UnPreview, Stop). Click a bind, then press any key — or any controller button.
- **All controller types supported** — controller binds now work with Xbox, PlayStation (PS4/PS5), Nintendo Switch Pro, Steam, and generic pads, via both XInput and DirectInput backends. Sony pads show real button names (Cross, Square, L1, Options, Touchpad…).

### Fixed
- **Camo no longer fails on maps with unusual character bounds** — the packed-radius calibration now falls back to a known-good scale instead of aborting the whole paint with `packed_radius_calibration_out_of_range`.
- **Crash at round end during camo paint** — the paint pipeline now stops cleanly when the paint component or relay begins destruction at level teardown, instead of writing into freed game state (the `EXCEPTION_ACCESS_VIOLATION` crash).

## Install / Update
1. Extract `peterhack-v2.6.5-win64.zip` (or let the loader auto-update).
2. Run `peterhack-loader.exe --local --wait`.
3. Launch the game (or wait for it).

**Important:** fully close the game before updating. The camo bridge stays loaded in memory until the game process exits, so re-injecting without a full restart keeps the old build running.

Saved main settings reset once (the settings format changed for the unified keybinds).

---

## How to use peterhack

### 1. Launch and inject
1. Extract the zip to a folder (e.g. your Desktop).
2. Double-click `peterhack-loader.exe`, or run `peterhack-loader.exe --local --wait` from a terminal.
3. Start the game. When the DirectX 12 overlay hooks, the console prints hook messages and the menu becomes available.

### 2. Open the menu
- Press **INSERT** or **F10** to open/close the in-game menu.
- (Optional) With **Controller Binds** enabled, the controller's **Back/View** button also opens/closes the menu.
- While the menu is open, mouse/controller look is locked to the menu; close it to return control to the game.

### 3. Menu tabs
- **ESP** — visuals: FOV changer, box, lines, names, roles, skeleton, distance, decoys, and colors for visible / not-visible / lines / decoy.
- **Teleport** — one button per player to teleport to them.
- **Tools** — survivor/hunter exploits (anti-detection, no decoy cooldown, clone count, no gun cooldown, infinite bullets), the **Magnet Key** bind, the **Controller Binds** toggle, **Kill All Survivors**, **Kill Specific Player**, anti server kick, return to lobby.
- **Name Changer** — spoof your own name or copy another player's.
- **Camouflage** — mesh paint camo with brush/region tuning, hotkeys, and the custom keybind recorder.

### 4. Controller binds
1. Go to the **Tools** tab and enable **Controller Binds** (works with Xbox, PS4/PS5, Switch Pro, Steam, generic pads).
2. Click a bind button (e.g. **Menu Button** or **Magnet Key**), then press the controller button you want. ESC cancels. Insert/F10 stay reserved for the menu.

### 5. Magnet (hunter)
- Enable it with the **Magnet Key** (default **G**, rebindable to key/mouse/controller). While active and playing hunter, survivors in front of your view are pulled toward you. Tap the key again to toggle off.

### 6. Camouflage
1. Open the **Camouflage** tab — the bridge loads automatically (status shows "Bridge: ready").
2. Tune brushes/regions/colors if desired, or use defaults.
3. Use the buttons (**Start / Preview / UnPreview / Stop**) or enable **Enable camo hotkeys**.
4. With hotkeys on, record a custom key or controller button for each action. They fire in a match with the menu closed.

### 7. Unload
- Press **END** to unload peterhack, or just close the game.

### Runtime folders
| Path | Contents |
|------|----------|
| `Desktop\peterhack\` | Built binaries (loader deploy output) |
| `C:\peterhack\settings.ini` | Menu / ESP / tools settings |
| `C:\peterhack\camo.cfg` | Camouflage tuning + keybinds |

### Troubleshooting
| Problem | Fix |
|---------|-----|
| Camo shows an old error after updating | Fully close the game (the bridge stays resident in memory), then relaunch and re-inject |
| No menu | Confirm injection (console shows DX12 hook messages); focus the game window |
| Controller not detected | Enable **Controller Binds** in Tools; for Switch Pro use Bluetooth or Steam Input |
| Tools/camo do nothing | Enable the toggles; for camo hotkeys, wait until armed in a match with the menu closed |
