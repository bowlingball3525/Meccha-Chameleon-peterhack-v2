#include <Windows.h>
#include <cstdio>

#include "imgui/imgui.h"
#include "GamepadInput.hpp"
#include "Keybinds.hpp"

namespace
{
	// Identity of the bind currently being recorded (only one at a time,
	// across every tab). Identified by the bound variable's address, which is
	// stable because binds live in the settings objects.
	int* s_activeBind = nullptr;
	ULONGLONG s_armTickMs = 0;

	bool PollKeyPress(bool allowMouse, int& outVk)
	{
		if (allowMouse)
		{
			static const int kMouseKeys[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
			for (int vk : kMouseKeys)
			{
				if (GetAsyncKeyState(vk) & 1)
				{
					outVk = vk;
					return true;
				}
			}
		}

		if (GetAsyncKeyState(VK_ESCAPE) & 1)
		{
			outVk = VK_ESCAPE;
			return true;
		}

		// 0x08+ skips the mouse VK range (0x01-0x06).
		for (int vk = 0x08; vk <= 0xFE; ++vk)
		{
			if (GetAsyncKeyState(vk) & 1)
			{
				outVk = vk;
				return true;
			}
		}
		return false;
	}
}

namespace Binds
{
	bool IsValidBind(int code)
	{
		if (IsPadBind(code))
			return Gamepad::IsValidButton(PadMask(code));
		return code > 0 && code < 256;
	}

	bool Pressed(int code, bool allowReservedPad)
	{
		if (IsPadBind(code))
			return Gamepad::Pressed(PadMask(code), allowReservedPad);
		if (code <= 0 || code >= 256)
			return false;
		return (GetAsyncKeyState(code) & 1) != 0;
	}

	const char* BindName(int code)
	{
		static char name[40];

		if (IsPadBind(code))
		{
			snprintf(name, sizeof(name), "Pad %s", Gamepad::ButtonName(PadMask(code)));
			return name;
		}

		switch (code)
		{
		case VK_LBUTTON: return "Mouse Left";
		case VK_RBUTTON: return "Mouse Right";
		case VK_MBUTTON: return "Mouse Middle";
		case VK_XBUTTON1: return "Mouse 4";
		case VK_XBUTTON2: return "Mouse 5";
		}

		UINT sc = MapVirtualKeyA(code, MAPVK_VK_TO_VSC);
		switch (code) // extended keys need the extended-scancode bit to name correctly
		{
		case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
		case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
		case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
			sc |= 0x100;
			break;
		}
		if (sc && GetKeyNameTextA((LONG)(sc << 16), name, sizeof(name)) > 0)
			return name;
		snprintf(name, sizeof(name), "0x%02X", code);
		return name;
	}

	void ClearKeyEdges()
	{
		for (int vk = 0x01; vk <= 0x06; ++vk)
			(void)GetAsyncKeyState(vk);
		for (int vk = 0x08; vk <= 0xFE; ++vk)
			(void)GetAsyncKeyState(vk);
	}

	bool RecorderRow(const char* label, int& bindCode, bool allowPad, bool allowMouse)
	{
		ImGui::Text("%s:", label);
		ImGui::SameLine();

		const bool active = (s_activeBind == &bindCode);
		const char* buttonText = active
			? (allowPad ? "Press key/pad..." : "Press key/mouse...")
			: BindName(bindCode);
		ImGui::PushID(&bindCode);
		if (ImGui::Button(buttonText))
		{
			// Arm after a short delay so the click itself can't be captured.
			s_activeBind = &bindCode;
			s_armTickMs = GetTickCount64() + 150;
			ClearKeyEdges();
			if (allowPad)
				Gamepad::BeginCapture();
		}
		ImGui::PopID();

		if (!active || GetTickCount64() < s_armTickMs)
			return false;

		int vk = 0;
		if (PollKeyPress(allowMouse, vk))
		{
			s_activeBind = nullptr;
			Gamepad::CancelCapture();
			// ESC cancels; Insert/F10 stay reserved for the menu toggle.
			if (vk == VK_ESCAPE || vk == VK_INSERT || vk == VK_F10)
				return false;
			bindCode = vk;
			return true;
		}

		int padMask = 0;
		if (allowPad && Gamepad::CapturePressed(padMask))
		{
			s_activeBind = nullptr;
			const int code = MakePadBind(padMask);
			// The menu pad button stays reserved (like Insert/F10) for every
			// row except the menu bind itself (whose current code matches it).
			if (padMask == Gamepad::ReservedButton() && bindCode != code)
				return false;
			bindCode = code;
			return true;
		}
		return false;
	}

	void CancelRecorder()
	{
		if (s_activeBind)
		{
			s_activeBind = nullptr;
			Gamepad::CancelCapture();
		}
	}
}
