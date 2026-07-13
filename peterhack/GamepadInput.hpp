#pragma once

// Controller (XInput) support for keybinds. The XInput DLL is loaded dynamically
// (xinput1_4 / 1_3 / 9_1_0), matching the ImGui Win32 backend's approach, so no
// import-library dependency is added to the project.
namespace Gamepad
{
	// Bind values are XINPUT_GAMEPAD_* button masks (single bit). The two analog
	// triggers get pseudo-button bits above the 16-bit wButtons range.
	constexpr int kLeftTrigger = 0x10000;
	constexpr int kRightTrigger = 0x20000;

	// Default binds (rebindable in the menu).
	constexpr int kDefaultMenuButton = 0x0020;   // Back / View / Select
	constexpr int kDefaultMagnetButton = 0x0002; // D-Pad Down

	// Call once per frame before querying Pressed().
	void Poll();

	bool IsConnected();

	// True on the frame the button went from released to pressed. Suppressed
	// while a bind capture is active so the press being bound can't also fire
	// the action it was previously bound to.
	bool Pressed(int button);

	// Bind capture: click-to-rebind flow for the menu.
	void BeginCapture();
	void CancelCapture();
	bool CaptureActive();
	// True once any button is newly pressed; stores it and ends the capture.
	bool CapturePressed(int& outButton);

	const char* ButtonName(int button);
}
