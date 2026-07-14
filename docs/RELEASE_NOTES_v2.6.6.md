# peterhack v2.6.6

> **⚠️ UNTESTED RELEASE** — this build compiles and packages cleanly but has not been fully tested in-game. Keep the previous version for rollback and please report issues.

## New since v2.6.5

### UI overhaul
- **New "Deep Dark" theme** — a much sleeker, high-contrast dark look with rounded frames, a blue accent, and tighter spacing (replaces the default ImGui style).
- **Font Awesome icons** on the tabs and key buttons (👁 ESP, ➤ Teleport, 🔧 Exploits, 🪪 Name Changer, 🖌 Camouflage, 🎛 Misc, ⚙ Config, plus skull/exit/save/load icons). Ships as `fonts/fa-solid-900.ttf`.
- **Crisper text** — base UI font bumped up slightly.

### Menu reorganization
- **Tools tab renamed to "Exploits."**
- **New "Misc" tab** holds the **Controller Binds** toggle + Menu Button rebind.
- **New "Config" tab** holds **Save / Load**. The **Save** button now saves *both* the main settings **and** the camouflage settings.
- **Camouflage tab**: UnPreview button moved directly below Preview; the standalone "Save camo settings" button was removed (use Config → Save).
- **Brush 2** minimum lowered from 5 to **2** texels.
- Wider default menu window.

### Auto-update fixes
- The updater now shows a **native Windows Yes/No popup** when a newer release is available, instead of a console prompt that could be missed. Added clear success/failure dialogs.

## Install / Update
1. Extract `peterhack-v2.6.6-win64.zip` (or let the loader auto-update and click **Yes** on the popup).
2. Run `peterhack-loader.exe --local --wait`.
3. Launch the game (or wait for it).

**Important:** fully close the game before updating — the camo bridge stays loaded in memory until the game process exits.

Saved settings are preserved from v2.6.5.

---

## How to use peterhack

### 1. Launch and inject
1. Extract the zip to a folder (e.g. your Desktop).
2. Double-click `peterhack-loader.exe`, or run `peterhack-loader.exe --local --wait` from a terminal.
3. Start the game. When the overlay hooks, the menu becomes available.

### 2. Open the menu
- Press **INSERT** or **F10** to open/close the in-game menu.
- (Optional) With **Controller Binds** enabled, the controller's **Back/View** button also toggles the menu.

### 3. Menu tabs
- **ESP** — visuals: FOV changer, box, lines, names, roles, skeleton, distance, decoys, colors.
- **Teleport** — one button per player.
- **Exploits** — survivor/hunter exploits, the **Magnet Key** bind, Kill All / Kill Specific, anti server kick, return to lobby.
- **Name Changer** — spoof your name or copy another player's.
- **Camouflage** — mesh paint camo with brush/region tuning, hotkeys, and the keybind recorder.
- **Misc** — **Controller Binds** toggle + Menu Button rebind.
- **Config** — **Save / Load** (Save also stores camouflage settings).

### 4. Controller binds
1. In **Misc**, enable **Controller Binds** (Xbox, PS4/PS5, Switch Pro, Steam, generic pads).
2. Click a bind (e.g. Menu Button, or a camo hotkey), then press the controller button. ESC cancels; Insert/F10 stay reserved for the menu.

### 5. Magnet (hunter)
- Toggle with the **Magnet Key** (default **G**, rebindable to key/mouse/controller). While active as hunter, survivors in front of your view are pulled toward you.

### 6. Camouflage
1. Open the **Camouflage** tab — the bridge loads automatically ("Bridge: ready").
2. Tune brushes/regions/colors or use defaults.
3. Use the **Start / Preview / UnPreview / Stop** buttons, or enable **Enable camo hotkeys** and record a key/controller button per action.

### 7. Unload
- Press **END** to unload peterhack, or just close the game.

### Runtime folders
| Path | Contents |
|------|----------|
| `Desktop\peterhack\` | Built binaries + `fonts\` |
| `C:\peterhack\settings.ini` | Menu / ESP / exploit settings |
| `C:\peterhack\camo.cfg` | Camouflage tuning + keybinds |

### Troubleshooting
| Problem | Fix |
|---------|-----|
| Camo shows an old error after updating | Fully close the game (the bridge stays resident in memory), then relaunch and re-inject |
| Tab/button icons show as boxes | Make sure `fonts\fa-solid-900.ttf` is next to `peterhack.dll` |
| No menu | Confirm injection; focus the game window |
| Controller not detected | Enable **Controller Binds** in Misc; for Switch Pro use Bluetooth or Steam Input |
| No update popup | It only appears when the GitHub release is newer than your installed version |
