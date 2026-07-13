#include "includes.hpp"
#include "GamepadInput.hpp"

#include <xinput.h>

namespace
{
	typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD, XINPUT_STATE*);

	HMODULE g_xinputDll = nullptr;
	PFN_XInputGetState g_pfnGetState = nullptr;
	bool g_loadAttempted = false;

	bool g_connected = false;
	DWORD g_userIndex = 0;
	// Trigger pseudo-buttons are merged into the same mask as wButtons.
	int g_buttons = 0;
	int g_prevButtons = 0;

	bool g_captureActive = false;
	// Buttons held when capture began must be released before they can be captured,
	// so the click that opened the rebind can't instantly bind a held button.
	int g_captureIgnoreMask = 0;

	void EnsureLoaded()
	{
		if (g_loadAttempted)
			return;
		g_loadAttempted = true;

		const char* dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
		for (const char* name : dlls)
		{
			g_xinputDll = ::LoadLibraryA(name);
			if (g_xinputDll)
			{
				g_pfnGetState = (PFN_XInputGetState)::GetProcAddress(g_xinputDll, "XInputGetState");
				if (g_pfnGetState)
					return;
				::FreeLibrary(g_xinputDll);
				g_xinputDll = nullptr;
			}
		}
		PhLog("[gamepad] XInput DLL not found - controller binds unavailable\n");
	}

	int ReadButtons(const XINPUT_GAMEPAD& pad)
	{
		int buttons = pad.wButtons;
		if (pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
			buttons |= Gamepad::kLeftTrigger;
		if (pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
			buttons |= Gamepad::kRightTrigger;
		return buttons;
	}
}

namespace Gamepad
{
	void Poll()
	{
		EnsureLoaded();

		g_prevButtons = g_buttons;
		g_buttons = 0;

		if (!g_pfnGetState)
		{
			g_connected = false;
			return;
		}

		XINPUT_STATE state{};
		// Stick to the last known-good pad slot; rescan all four only when it drops.
		if (g_connected && g_pfnGetState(g_userIndex, &state) == ERROR_SUCCESS)
		{
			g_buttons = ReadButtons(state.Gamepad);
			return;
		}

		g_connected = false;
		for (DWORD i = 0; i < 4; ++i)
		{
			if (g_pfnGetState(i, &state) == ERROR_SUCCESS)
			{
				g_userIndex = i;
				g_connected = true;
				g_buttons = ReadButtons(state.Gamepad);
				return;
			}
		}
	}

	bool IsConnected()
	{
		return g_connected;
	}

	bool Pressed(int button)
	{
		if (g_captureActive || button == 0)
			return false;
		return (g_buttons & button) != 0 && (g_prevButtons & button) == 0;
	}

	void BeginCapture()
	{
		g_captureActive = true;
		g_captureIgnoreMask = g_buttons;
	}

	void CancelCapture()
	{
		g_captureActive = false;
	}

	bool CaptureActive()
	{
		return g_captureActive;
	}

	bool CapturePressed(int& outButton)
	{
		if (!g_captureActive)
			return false;

		g_captureIgnoreMask &= g_buttons; // released buttons become capturable again

		int fresh = g_buttons & ~g_prevButtons & ~g_captureIgnoreMask;
		if (!fresh)
			return false;

		outButton = fresh & ~(fresh - 1); // lowest set bit, in case two land on the same frame
		g_captureActive = false;
		return true;
	}

	const char* ButtonName(int button)
	{
		switch (button)
		{
		case XINPUT_GAMEPAD_DPAD_UP: return "D-Pad Up";
		case XINPUT_GAMEPAD_DPAD_DOWN: return "D-Pad Down";
		case XINPUT_GAMEPAD_DPAD_LEFT: return "D-Pad Left";
		case XINPUT_GAMEPAD_DPAD_RIGHT: return "D-Pad Right";
		case XINPUT_GAMEPAD_START: return "Start";
		case XINPUT_GAMEPAD_BACK: return "Back/View";
		case XINPUT_GAMEPAD_LEFT_THUMB: return "L3";
		case XINPUT_GAMEPAD_RIGHT_THUMB: return "R3";
		case XINPUT_GAMEPAD_LEFT_SHOULDER: return "LB";
		case XINPUT_GAMEPAD_RIGHT_SHOULDER: return "RB";
		case XINPUT_GAMEPAD_A: return "A";
		case XINPUT_GAMEPAD_B: return "B";
		case XINPUT_GAMEPAD_X: return "X";
		case XINPUT_GAMEPAD_Y: return "Y";
		case kLeftTrigger: return "LT";
		case kRightTrigger: return "RT";
		case 0: return "None";
		}
		static char name[16];
		snprintf(name, sizeof(name), "0x%X", button);
		return name;
	}
}
