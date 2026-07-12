#include "includes.hpp"

// Human-readable name for a virtual-key code, falling back to hex for unknown keys.
static const char* KeyName(int vk)
{
	static char name[32];
	switch (vk)
	{
	case VK_LBUTTON: return "Mouse Left";
	case VK_RBUTTON: return "Mouse Right";
	case VK_MBUTTON: return "Mouse Middle";
	case VK_XBUTTON1: return "Mouse 4";
	case VK_XBUTTON2: return "Mouse 5";
	}

	UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
	switch (vk) // extended keys need the extended-scancode bit to name correctly
	{
	case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
	case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
	case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
		sc |= 0x100;
		break;
	}
	if (sc && GetKeyNameTextA((LONG)(sc << 16), name, sizeof(name)) > 0)
		return name;
	snprintf(name, sizeof(name), "0x%02X", vk);
	return name;
}

static void ClearBindKeyEdges()
{
	for (int vk = 0x01; vk <= 0x06; ++vk)
		(void)GetAsyncKeyState(vk);
	for (int vk = 0x08; vk <= 0xFE; ++vk)
		(void)GetAsyncKeyState(vk);
}

static bool PollBindKeyPress(int& outVk)
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

	if (GetAsyncKeyState(VK_ESCAPE) & 1)
	{
		outVk = VK_ESCAPE;
		return true;
	}

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

void Menu::Init()
{
	ImGui::SetNextWindowSize({ 300, 480 }, ImGuiCond_Once);
	ImGui::Begin("peterhack", nullptr, 0);

	const float footerH = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;

	ImGui::BeginChild("##content", ImVec2(0, -footerH), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	if (ImGui::BeginTabBar("##tabs"))
	{
		if (ImGui::BeginTabItem("ESP"))
		{
			ImGui::BeginChild("##esp_list", ImVec2(0, 0), false);

			ImGui::Checkbox("Fov Changer", &cfg->bFovChanger);
			if (cfg->bFovChanger)
				ImGui::SliderFloat("Fov Value", &cfg->fFovValue, 50.0f, 180.0f);

			ImGui::Checkbox("Enemy Only", &cfg->bEnemyOnly);
			ImGui::Checkbox("Character Visibility (Infection Mode)", &cfg->bForceCharacterVisibility);
			ImGui::Checkbox("Box", &cfg->bBox);
			ImGui::Checkbox("Lines", &cfg->bLines);
			ImGui::Checkbox("Name", &cfg->bNames);
			ImGui::Checkbox("Roles", &cfg->bRoles);
			ImGui::Checkbox("Skeleton", &cfg->bSkeleton);
			ImGui::Checkbox("Distance", &cfg->bDistance);
			// ImGui::Checkbox("Hunter Ammo", &cfg->bHunterAmmo);
			ImGui::Checkbox("Decoys", &cfg->bDecoys);

			ImGui::Separator();
			ImGui::Text("Colors");

			if (ImGui::ColorButton("##colVisible", *(ImVec4*)cfg->colVisible))
				ImGui::OpenPopup("popup_colVisible");
			ImGui::SameLine();
			ImGui::Text("Visible");
			if (ImGui::BeginPopup("popup_colVisible"))
			{
				ImGui::ColorPicker4("##pick", cfg->colVisible);
				ImGui::EndPopup();
			}

			if (ImGui::ColorButton("##colNotVisible", *(ImVec4*)cfg->colNotVisible))
				ImGui::OpenPopup("popup_colNotVisible");
			ImGui::SameLine();
			ImGui::Text("Not Visible");
			if (ImGui::BeginPopup("popup_colNotVisible"))
			{
				ImGui::ColorPicker4("##pick", cfg->colNotVisible);
				ImGui::EndPopup();
			}

			if (ImGui::ColorButton("##colLines", *(ImVec4*)cfg->colLines))
				ImGui::OpenPopup("popup_colLines");
			ImGui::SameLine();
			ImGui::Text("Lines");
			if (ImGui::BeginPopup("popup_colLines"))
			{
				ImGui::ColorPicker4("##pick", cfg->colLines);
				ImGui::EndPopup();
			}

			if (ImGui::ColorButton("##colDecoy", *(ImVec4*)cfg->colDecoy))
				ImGui::OpenPopup("popup_colDecoy");
			ImGui::SameLine();
			ImGui::Text("Decoy");
			if (ImGui::BeginPopup("popup_colDecoy"))
			{
				ImGui::ColorPicker4("##pick", cfg->colDecoy);
				ImGui::EndPopup();
			}

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Teleport"))
		{
			ImGui::BeginChild("##tp_list", ImVec2(0, 0), false);

			if (cheat->PlayerInfos.empty())
			{
				ImGui::TextDisabled("No players found");
			}
			else
			{
				for (int i = 0; i < (int)cheat->PlayerInfos.size(); i++)
				{
					ImGui::PushID(i);
					if (ImGui::Button("TP"))
						cheat->RequestTeleport(cheat->PlayerInfos[i].Actor);
					ImGui::SameLine();
					ImGui::Text("%s", cheat->PlayerInfos[i].Name.c_str());
					ImGui::PopID();
				}
			}

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Tools"))
		{
			ImGui::BeginChild("##tools_list", ImVec2(0, 0), false);

			ImGui::Text("Survivors");
			ImGui::Separator();
			ImGui::Checkbox("Anti Detection", &cfg->bAntiDetection);
			ImGui::Checkbox("No Decoy Cooldown", &cfg->bNoDecoyCooldown);
			ImGui::Checkbox("Set Clone Amount", &cfg->bSetDecoyNum);
			if (cfg->bSetDecoyNum)
			{
				ImGui::PushID("decoy_count");
				ImGui::SliderInt("Clone count", &cfg->iDecoyCount, 0, 99);
				ImGui::PopID();
			}

			ImGui::Separator();
			ImGui::Text("Hunters");
			ImGui::Separator();
			ImGui::Checkbox("No Gun Cooldown", &cfg->bNoGunCooldown);
			ImGui::Checkbox("Infinite Bullets", &cfg->bInfiniteBullets);

			// Magnet toggle key rebind: click the button, then press any key or mouse button (ESC cancels).
			static bool bindingMagnet = false;
			static ULONGLONG bindingMagnetReadyTick = 0;
			ImGui::Text("Magnet Key:");
			ImGui::SameLine();
			if (ImGui::Button(bindingMagnet ? "Press key/mouse..." : KeyName(cfg->iMagnetKey)))
			{
				bindingMagnet = true;
				bindingMagnetReadyTick = GetTickCount64() + 150;
				ClearBindKeyEdges();
			}
			if (bindingMagnet && GetTickCount64() >= bindingMagnetReadyTick)
			{
				int pressedVk = 0;
				if (PollBindKeyPress(pressedVk))
				{
					if (pressedVk != VK_ESCAPE)
						cfg->iMagnetKey = pressedVk;
					bindingMagnet = false;
				}
			}

			if (ImGui::Button("Kill All Survivors"))
				cheat->RequestKillAllSurvivors();

			ImGui::Separator();
			ImGui::Text("Kill Specific Player");

			// Track the pick by actor pointer, not list index - PlayerInfos is rebuilt every frame and
			// indices can drift. Resolve the selected actor's current name for the combo preview, and
			// drop the selection if that actor no longer exists this frame.
			static SDK::AActor* selectedKillActor = nullptr;
			const char* killPreview = "Select survivor";
			bool killStillPresent = false;
			int survivorCount = 0;
			for (const auto& p : cheat->PlayerInfos)
			{
				if (!p.IsSurvivor)
					continue; // only survivors can be killed
				survivorCount++;
				if (p.Actor == selectedKillActor)
				{
					killPreview = p.Name.c_str();
					killStillPresent = true;
				}
			}
			if (!killStillPresent)
				selectedKillActor = nullptr;
			if (survivorCount == 0)
				killPreview = "No survivors found";

			// Combo on the left filling the row, fixed-width "Kill" button on the right.
			const float killBtnW = ImGui::CalcTextSize("Kill").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - killBtnW - ImGui::GetStyle().ItemSpacing.x);
			if (ImGui::BeginCombo("##kill_target", killPreview))
			{
				for (int i = 0; i < (int)cheat->PlayerInfos.size(); i++)
				{
					if (!cheat->PlayerInfos[i].IsSurvivor)
						continue;
					ImGui::PushID(i);
					const bool isSelected = (cheat->PlayerInfos[i].Actor == selectedKillActor);
					if (ImGui::Selectable(cheat->PlayerInfos[i].Name.c_str(), isSelected))
						selectedKillActor = cheat->PlayerInfos[i].Actor;
					if (isSelected)
						ImGui::SetItemDefaultFocus();
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}

			ImGui::SameLine();
			if (ImGui::Button("Kill", ImVec2(killBtnW, 0)) && selectedKillActor)
				cheat->RequestKillSurvivor(selectedKillActor);

			ImGui::Separator();
			ImGui::Text("General");
			ImGui::Separator();
			ImGui::Checkbox("Anti Server Kick", &cfg->bPreventKick);

			if (ImGui::Button("Return to Main Lobby"))
				cheat->RequestReturnToMainLobby();

			ImGui::Separator();

			if (ImGui::Button("Dump Bones (Debugging)"))
				cfg->bDumpBones = true;

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Name Changer"))
		{
			ImGui::BeginChild("##name_list", ImVec2(0, 0), false);

			static SDK::AActor* selectedNameActor = nullptr;
			const char* namePreview = "Select player";
			std::string selectedName;
			bool nameStillPresent = false;
			for (const auto& p : cheat->PlayerInfos)
			{
				if (p.Actor == selectedNameActor)
				{
					namePreview = p.Name.c_str();
					selectedName = p.Name;
					nameStillPresent = true;
				}
			}
			if (!nameStillPresent)
				selectedNameActor = nullptr;
			if (cheat->PlayerInfos.empty())
				namePreview = "No players found";

			const float nameBtnW = ImGui::CalcTextSize("Change").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - nameBtnW - ImGui::GetStyle().ItemSpacing.x);
			if (ImGui::BeginCombo("##name_target", namePreview))
			{
				for (int i = 0; i < (int)cheat->PlayerInfos.size(); i++)
				{
					ImGui::PushID(i);
					const bool isSelected = (cheat->PlayerInfos[i].Actor == selectedNameActor);
					if (ImGui::Selectable(cheat->PlayerInfos[i].Name.c_str(), isSelected))
						selectedNameActor = cheat->PlayerInfos[i].Actor;
					if (isSelected)
						ImGui::SetItemDefaultFocus();
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}

			ImGui::SameLine();
			if (ImGui::Button("Change", ImVec2(nameBtnW, 0)) && selectedNameActor && !selectedName.empty())
				cheat->RequestChangeName(selectedName);

			// Custom name: type anything and apply it to our own player.
			static char customName[64] = "";
			const float setBtnW = ImGui::CalcTextSize("Set").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - setBtnW - ImGui::GetStyle().ItemSpacing.x);
			const bool nameEntered = ImGui::InputText("##custom_name", customName, sizeof(customName), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if ((ImGui::Button("Set", ImVec2(setBtnW, 0)) || nameEntered) && customName[0] != '\0')
				cheat->RequestChangeName(customName);

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Camouflage"))
		{
			ImGui::BeginChild("##camo_list", ImVec2(0, 0), false);
			if (g_camo)
				g_camo->DrawMenu();
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::EndChild();

	ImGui::Separator();

	float buttonW = 55.0f;
	if (ImGui::Button("Save", ImVec2(buttonW, 0)))
		cfg->SaveSettings();
	ImGui::SameLine();
	if (ImGui::Button("Load", ImVec2(buttonW, 0)))
		cfg->LoadSettings();

	ImGui::SameLine();
	float checkboxW = ImGui::CalcTextSize("Enable").x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - checkboxW - ImGui::GetStyle().WindowPadding.x);
	ImGui::Checkbox("ESP overlay", &cfg->bInitHooks);

	ImGui::End();
}
