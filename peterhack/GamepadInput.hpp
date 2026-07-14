#pragma once

// Controller support for keybinds. Two backends feed one unified button
// state:
//   - XInput (loaded dynamically): Xbox 360/One/Series pads, plus anything
//     presented as XInput by Steam Input (Steam Controller, PS/Switch pads
//     with Steam Input enabled) or DS4Windows.
//   - DirectInput8: everything else Windows exposes as a HID game controller
//     when connected directly - PS4 (DualShock 4), PS5 (DualSense), Switch
//     Pro over Bluetooth, and generic pads. XInput devices are filtered out
//     of this path (the "IG_" device-id check) so a press never counts twice.
//
// Buttons are identified by a small unified ID rather than a bitmask:
//   0-15   XInput buttons (bit index of XINPUT_GAMEPAD_* masks)
//   16-17  XInput left/right trigger (pseudo-buttons)
//   18-49  DirectInput buttons 1-32 (PS pads: Square/Cross/... live here)
//   50-53  DirectInput POV hat up/right/down/left
namespace Gamepad
{
	constexpr int kLeftTrigger = 16;
	constexpr int kRightTrigger = 17;
	constexpr int kDiButtonFirst = 18;
	constexpr int kDiButtonCount = 32;
	constexpr int kHatFirst = 50;
	constexpr int kButtonIdCount = 54;

	// Default menu-toggle button: XInput Back / View / Select.
	constexpr int kDefaultMenuButton = 5;

	constexpr bool IsValidButton(int id) { return id >= 0 && id < kButtonIdCount; }

	// Window associated with the DirectInput devices (the game window).
	void SetWindow(void* hwnd);

	// Release DirectInput devices; called on DLL unload.
	void Shutdown();

	// Call once per frame before querying Pressed(). When disabled (the
	// "Controller Binds" master toggle is off) the pad state is cleared and
	// no controller API is touched, so no pad bind can fire anywhere.
	void Poll(bool enabled);

	bool IsConnected();
	// Whether the last Poll() ran with controller support enabled.
	bool IsEnabled();

	// The menu-toggle pad button. Pressed() suppresses it for every other
	// consumer (mirroring how Insert/F10 are blocked for keyboard binds), so
	// closing the menu can't fire an action bound to the same button.
	void SetReservedButton(int button);
	int ReservedButton();

	// True on the frame the button went from released to pressed. Suppressed
	// while a bind capture is active so the press being bound can't also fire
	// the action it was previously bound to. The reserved (menu) button only
	// reports pressed when allowReserved is set by the menu-toggle check.
	bool Pressed(int button, bool allowReserved = false);

	// Bind capture: click-to-rebind flow for the menu.
	void BeginCapture();
	void CancelCapture();
	bool CaptureActive();
	// True once any button is newly pressed; stores it and ends the capture.
	bool CapturePressed(int& outButton);

	const char* ButtonName(int button);
}
