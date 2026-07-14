#define DIRECTINPUT_VERSION 0x0800

#include "includes.hpp"
#include "GamepadInput.hpp"

#include <xinput.h>
#include <dinput.h>

// dinput8.dll and dxguid data ship with every supported Windows version.
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

namespace
{
	typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD, XINPUT_STATE*);

	HMODULE g_xinputDll = nullptr;
	PFN_XInputGetState g_pfnGetState = nullptr;
	bool g_xinputLoadAttempted = false;

	bool g_xinputConnected = false;
	DWORD g_xinputUserIndex = 0;
	// XInputGetState on empty slots is slow (~ms each); when nothing is
	// connected, rescan all four slots at most once per second.
	ULONGLONG g_xinputNextRescanMs = 0;

	// ---- DirectInput backend (PS4/PS5/Switch/generic HID pads) ----
	IDirectInput8A* g_di = nullptr;
	bool g_diCreateAttempted = false;
	ULONGLONG g_diNextEnumMs = 0;
	bool g_diSonyLayout = false; // any Sony pad attached: use DS button names

	constexpr int kMaxDiDevices = 4;
	IDirectInputDevice8A* g_diDevices[kMaxDiDevices] = {};
	int g_diDeviceCount = 0;

	bool g_enabled = false;
	int g_reservedButton = 0;
	// Unified button state, one bit per Gamepad button ID.
	std::uint64_t g_buttons = 0;
	std::uint64_t g_prevButtons = 0;

	bool g_captureActive = false;
	// Buttons held when capture began must be released before they can be
	// captured, so the click that opened the rebind can't instantly bind a
	// held button.
	std::uint64_t g_captureIgnoreMask = 0;

	HWND g_hwnd = nullptr;

	void EnsureXInputLoaded()
	{
		if (g_xinputLoadAttempted)
			return;
		g_xinputLoadAttempted = true;

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
		PhLog("[gamepad] XInput DLL not found - Xbox controller binds unavailable\n");
	}

	std::uint64_t ReadXInputButtons(const XINPUT_GAMEPAD& pad)
	{
		std::uint64_t buttons = pad.wButtons; // mask bit index == button ID 0-15
		if (pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
			buttons |= 1ull << Gamepad::kLeftTrigger;
		if (pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
			buttons |= 1ull << Gamepad::kRightTrigger;
		return buttons;
	}

	void PollXInput()
	{
		EnsureXInputLoaded();
		if (!g_pfnGetState)
			return;

		XINPUT_STATE state{};
		// Stick to the last known-good pad slot; rescan all four only when it drops.
		if (g_xinputConnected && g_pfnGetState(g_xinputUserIndex, &state) == ERROR_SUCCESS)
		{
			g_buttons |= ReadXInputButtons(state.Gamepad);
			return;
		}

		g_xinputConnected = false;
		const ULONGLONG now = GetTickCount64();
		if (now < g_xinputNextRescanMs)
			return;
		for (DWORD i = 0; i < 4; ++i)
		{
			if (g_pfnGetState(i, &state) == ERROR_SUCCESS)
			{
				g_xinputUserIndex = i;
				g_xinputConnected = true;
				g_buttons |= ReadXInputButtons(state.Gamepad);
				return;
			}
		}
		g_xinputNextRescanMs = now + 1000;
	}

	// VID+PID keys (guidProduct.Data1 layout) of HID devices Windows tags as
	// XInput ("IG_" in the device path). Those pads are already covered by the
	// XInput poll and must not be double-read through DirectInput.
	std::unordered_set<DWORD> BuildXInputVidPidSet()
	{
		std::unordered_set<DWORD> out;
		UINT count = 0;
		if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || count == 0)
			return out;
		std::vector<RAWINPUTDEVICELIST> list(count);
		count = GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
		if (count == (UINT)-1)
			return out;
		for (const auto& device : list)
		{
			if (device.dwType != RIM_TYPEHID)
				continue;
			RID_DEVICE_INFO info{};
			info.cbSize = sizeof(info);
			UINT size = sizeof(info);
			if (GetRawInputDeviceInfoA(device.hDevice, RIDI_DEVICEINFO, &info, &size) == (UINT)-1)
				continue;
			char name[256]{};
			UINT nameSize = sizeof(name);
			if (GetRawInputDeviceInfoA(device.hDevice, RIDI_DEVICENAME, name, &nameSize) == (UINT)-1)
				continue;
			if (strstr(name, "IG_"))
				out.insert(MAKELONG(info.hid.dwVendorId, info.hid.dwProductId));
		}
		return out;
	}

	void ReleaseDiDevices()
	{
		for (int i = 0; i < g_diDeviceCount; ++i)
		{
			if (g_diDevices[i])
			{
				g_diDevices[i]->Unacquire();
				g_diDevices[i]->Release();
				g_diDevices[i] = nullptr;
			}
		}
		g_diDeviceCount = 0;
		g_diSonyLayout = false;
	}

	struct DiEnumContext
	{
		std::unordered_set<DWORD> xinputIds;
	};

	BOOL CALLBACK DiEnumDevicesCallback(LPCDIDEVICEINSTANCEA instance, LPVOID contextPtr)
	{
		auto* context = static_cast<DiEnumContext*>(contextPtr);
		if (g_diDeviceCount >= kMaxDiDevices)
			return DIENUM_STOP;
		// XInput pads are handled by the XInput poll.
		if (context->xinputIds.count(instance->guidProduct.Data1))
			return DIENUM_CONTINUE;

		IDirectInputDevice8A* device = nullptr;
		if (FAILED(g_di->CreateDevice(instance->guidInstance, &device, nullptr)) || !device)
			return DIENUM_CONTINUE;

		if (FAILED(device->SetDataFormat(&c_dfDIJoystick2)))
		{
			device->Release();
			return DIENUM_CONTINUE;
		}
		// Background + non-exclusive: we read the pad regardless of focus,
		// same as GetAsyncKeyState for keyboard binds. Failure is non-fatal -
		// some drivers reject the call and still deliver state.
		if (g_hwnd)
			device->SetCooperativeLevel(g_hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
		device->Acquire();

		const WORD vid = LOWORD(instance->guidProduct.Data1);
		if (vid == 0x054C) // Sony
			g_diSonyLayout = true;

		g_diDevices[g_diDeviceCount++] = device;
		PhLog("[gamepad] DirectInput pad attached: %s\n", instance->tszProductName);
		return DIENUM_CONTINUE;
	}

	void EnumerateDiDevices()
	{
		if (!g_diCreateAttempted)
		{
			g_diCreateAttempted = true;
			HMODULE self = nullptr;
			GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&g_diCreateAttempted),
				&self);
			if (FAILED(DirectInput8Create(self ? self : GetModuleHandleW(nullptr),
			                              DIRECTINPUT_VERSION, IID_IDirectInput8A,
			                              reinterpret_cast<void**>(&g_di), nullptr)))
			{
				g_di = nullptr;
				PhLog("[gamepad] DirectInput8Create failed - non-XInput pads unavailable\n");
			}
		}
		if (!g_di)
			return;

		ReleaseDiDevices();
		DiEnumContext context{ BuildXInputVidPidSet() };
		g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, DiEnumDevicesCallback, &context, DIEDFL_ATTACHEDONLY);
	}

	std::uint64_t ReadDiButtons(const DIJOYSTATE2& state)
	{
		std::uint64_t buttons = 0;
		for (int i = 0; i < Gamepad::kDiButtonCount; ++i)
		{
			if (state.rgbButtons[i] & 0x80)
				buttons |= 1ull << (Gamepad::kDiButtonFirst + i);
		}
		const DWORD pov = state.rgdwPOV[0];
		if (LOWORD(pov) != 0xFFFF) // centered
		{
			// Centidegrees clockwise from up; diagonals set both directions.
			if (pov >= 31500 || pov <= 4500)
				buttons |= 1ull << (Gamepad::kHatFirst + 0); // up
			if (pov >= 4500 && pov <= 13500)
				buttons |= 1ull << (Gamepad::kHatFirst + 1); // right
			if (pov >= 13500 && pov <= 22500)
				buttons |= 1ull << (Gamepad::kHatFirst + 2); // down
			if (pov >= 22500 && pov <= 31500)
				buttons |= 1ull << (Gamepad::kHatFirst + 3); // left
		}
		return buttons;
	}

	void PollDirectInput()
	{
		const ULONGLONG now = GetTickCount64();
		if (g_diDeviceCount == 0)
		{
			// No pads: rescan at most every 3s (enumeration touches every HID
			// device and is far too slow to run per frame).
			if (now < g_diNextEnumMs)
				return;
			g_diNextEnumMs = now + 3000;
			EnumerateDiDevices();
			if (g_diDeviceCount == 0)
				return;
		}

		bool lostAny = false;
		for (int i = 0; i < g_diDeviceCount; ++i)
		{
			IDirectInputDevice8A* device = g_diDevices[i];
			device->Poll(); // needed by some drivers, harmless otherwise

			DIJOYSTATE2 state{};
			HRESULT hr = device->GetDeviceState(sizeof(state), &state);
			if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
			{
				device->Acquire();
				hr = device->GetDeviceState(sizeof(state), &state);
			}
			if (FAILED(hr))
			{
				lostAny = true; // unplugged - re-enumerate everything below
				continue;
			}
			g_buttons |= ReadDiButtons(state);
		}

		if (lostAny)
		{
			ReleaseDiDevices();
			g_diNextEnumMs = now; // allow an immediate rescan
		}
	}
}

namespace Gamepad
{
	void SetWindow(void* hwnd)
	{
		g_hwnd = static_cast<HWND>(hwnd);
	}

	void Shutdown()
	{
		ReleaseDiDevices();
		if (g_di)
		{
			g_di->Release();
			g_di = nullptr;
		}
	}

	void Poll(bool enabled)
	{
		g_enabled = enabled;
		if (!enabled)
		{
			g_prevButtons = 0;
			g_buttons = 0;
			g_xinputConnected = false;
			return;
		}

		g_prevButtons = g_buttons;
		g_buttons = 0;
		PollXInput();
		PollDirectInput();
	}

	bool IsConnected()
	{
		return g_xinputConnected || g_diDeviceCount > 0;
	}

	bool IsEnabled()
	{
		return g_enabled;
	}

	void SetReservedButton(int button)
	{
		g_reservedButton = button;
	}

	int ReservedButton()
	{
		return g_reservedButton;
	}

	bool Pressed(int button, bool allowReserved)
	{
		if (g_captureActive || !IsValidButton(button))
			return false;
		if (!allowReserved && button == g_reservedButton)
			return false;
		const std::uint64_t bit = 1ull << button;
		return (g_buttons & bit) != 0 && (g_prevButtons & bit) == 0;
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

		const std::uint64_t fresh = g_buttons & ~g_prevButtons & ~g_captureIgnoreMask;
		if (!fresh)
			return false;

		for (int id = 0; id < kButtonIdCount; ++id)
		{
			if (fresh & (1ull << id))
			{
				outButton = id;
				g_captureActive = false;
				return true;
			}
		}
		return false;
	}

	const char* ButtonName(int button)
	{
		static const char* kXInputNames[18] = {
			"D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
			"Start", "Back/View", "L3", "R3", "LB", "RB",
			"Guide", "Button 11", "A", "B", "X", "Y", "LT", "RT",
		};
		// DualShock 4 / DualSense DirectInput button order.
		static const char* kSonyNames[14] = {
			"Square", "Cross", "Circle", "Triangle", "L1", "R1", "L2", "R2",
			"Share", "Options", "L3", "R3", "PS", "Touchpad",
		};
		static const char* kHatNames[4] = { "Hat Up", "Hat Right", "Hat Down", "Hat Left" };

		static char name[24];
		if (button >= 0 && button <= kRightTrigger)
			return kXInputNames[button];
		if (button >= kDiButtonFirst && button < kDiButtonFirst + kDiButtonCount)
		{
			const int index = button - kDiButtonFirst;
			if (g_diSonyLayout && index < 14)
				return kSonyNames[index];
			snprintf(name, sizeof(name), "Pad Btn %d", index + 1);
			return name;
		}
		if (button >= kHatFirst && button < kHatFirst + 4)
			return kHatNames[button - kHatFirst];
		snprintf(name, sizeof(name), "Pad %d?", button);
		return name;
	}
}
