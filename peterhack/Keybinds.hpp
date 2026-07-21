#pragma once

// Shared keybind helpers. A "bind code" stores either a keyboard/mouse
// virtual-key code or a Gamepad button ID tagged with kPadBindFlag, so a
// single persisted int can describe both input kinds.
namespace Binds
{
	constexpr int kPadBindFlag = 0x40000000;

	constexpr bool IsPadBind(int code) { return (code & kPadBindFlag) != 0; }
	constexpr int MakePadBind(int button) { return button | kPadBindFlag; }
	constexpr int PadMask(int code) { return code & ~kPadBindFlag; }

	// True for usable bind codes: a known single-button pad mask, or a VK in
	// range. Used to sanitize persisted config values.
	bool IsValidBind(int code);

	// Edge-detected press for either bind kind. Keyboard binds track down/up
	// transitions from the high bit so other GetAsyncKeyState callers cannot
	// steal presses; pad binds use the per-frame Gamepad poll.
	bool Pressed(int code, bool allowReservedPad = false);

	// Human-readable bind name ("F1", "Mouse 4", "Pad A", ...). Static buffer.
	const char* BindName(int code);

	// Mark keyboard binds as already held/released so the next Pressed() does
	// not fire until the physical key changes (used when re-arming hotkeys).
	void SyncKeyState(int code);

	// Re-sync every tracked keyboard bind from hardware (menu close, capture).
	void ClearKeyEdges();

	// ImGui row: "<label>: [current bind]". Clicking the button starts
	// recording; the next key (or controller button, when allowPad) becomes
	// the bind. ESC cancels, Insert/F10 are reserved for the menu toggle.
	// Returns true when the bind changed.
	bool RecorderRow(const char* label, int& bindCode, bool allowPad, bool allowMouse);

	// Abort any in-progress recording (e.g. when the menu closes mid-capture).
	void CancelRecorder();
}
