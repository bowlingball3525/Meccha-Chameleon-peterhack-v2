#include "includes.hpp"
#include "Notify.hpp"

#include <algorithm>

namespace
{
	constexpr ImGuiColorEditFlags kColorPickerPopupFlags =
		ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_AlphaBar |
		ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_PickerHueBar |
		ImGuiColorEditFlags_InputRGB;
}

void Menu::Init()
{
	ImGui::SetNextWindowSize({ 630, 520 }, ImGuiCond_Once);
	ImGui::Begin("peterhack", nullptr, 0);

	const float footerH = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;

	ImGui::BeginChild("##content", ImVec2(0, -footerH), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	if (ImGui::BeginTabBar("##tabs"))
	{
		if (ImGui::BeginTabItem(ICON_FA_CROSSHAIRS " Combat"))
		{
			ImGui::BeginChild("##combat_list", ImVec2(0, 0), false);

			ImGui::Text("Aimbot");
			ImGui::Separator();
			ImGui::Checkbox("Aimbot", &cfg->bAimbot);
			if (cfg->bAimbot)
			{
				Binds::RecorderRow("Aim Key (hold)", cfg->iAimKey, false, true);
				ImGui::SliderFloat("Smoothing", &cfg->fAimSmooth, 1.0f, 20.0f, "%.1f");
				ImGui::TextDisabled("1 = instant snap, higher = slower and more legit-looking");
			}

			ImGui::Separator();
			ImGui::Text("Triggerbot (hunter only)");
			ImGui::Separator();
			ImGui::Checkbox("Triggerbot", &cfg->bTriggerbot);
			if (cfg->bTriggerbot)
			{
				ImGui::TextDisabled("Auto-fires when your crosshair is on an enemy");
				if (cfg->iTriggerKey == 0)
					ImGui::TextDisabled("No hold key set — always active");
				Binds::RecorderRow("Trigger Key (hold)", cfg->iTriggerKey, false, true);
				if (cfg->iTriggerKey != 0)
				{
					ImGui::SameLine();
					if (ImGui::SmallButton("Clear (always on)"))
						cfg->iTriggerKey = 0;
				}
			}

			ImGui::Separator();
			ImGui::Text("Silent Aim (hunter only)");
			ImGui::Separator();
			ImGui::Checkbox("Silent Aim", &cfg->bSilentAim);
			if (cfg->bSilentAim)
				ImGui::TextDisabled("Locks the nearest enemy in the FOV circle and redirects\nyour shot traces / KillPlayer RPC without moving the camera.");

			ImGui::Text("Target selection (aimbot / silent / trigger)");
			if (cfg->bAimbot || cfg->bSilentAim || cfg->bTriggerbot)
			{
				ImGui::SliderFloat("FOV (px)", &cfg->fAimFov, 10.0f, 500.0f, "%.0f");
				ImGui::Combo("Aim bone", &cfg->iAimBone, "Head\0Chest\0");
				ImGui::Checkbox("Visible targets only", &cfg->bAimVisibleOnly);
				ImGui::Checkbox("Draw FOV circle", &cfg->bAimDrawFov);
			}
			else
				ImGui::TextDisabled("Enable aimbot, silent aim, or triggerbot to configure FOV");

			ImGui::Separator();
			ImGui::Text("Recoil");
			ImGui::Separator();
			ImGui::Checkbox("No Recoil / No Shake", &cfg->bNoRecoil);

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(ICON_FA_EYE " ESP"))
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
			ImGui::Checkbox("Outline", &cfg->bEspOutline);
			if (cfg->bEspOutline)
			{
				ImGui::SliderFloat("Outline thickness", &cfg->fEspOutlineThickness, 1.0f, 6.0f, "%.1f px");

				if (ImGui::ColorButton("##colEspOutline", *(ImVec4*)cfg->colEspOutline))
					ImGui::OpenPopup("popup_colEspOutline");
				ImGui::SameLine();
				ImGui::Text("Outline color / opacity");
				if (ImGui::BeginPopup("popup_colEspOutline"))
				{
					ImGui::ColorPicker4("##pick", cfg->colEspOutline, kColorPickerPopupFlags);
					ImGui::EndPopup();
				}

				struct OutlineOption { const char* label; int bit; };
				static const OutlineOption kOutlineOptions[] = {
					{ "Box", EspOutlineSection::Box },
					{ "Lines", EspOutlineSection::Lines },
					{ "Name", EspOutlineSection::Name },
					{ "Role", EspOutlineSection::Role },
					{ "Distance", EspOutlineSection::Distance },
					{ "Skeleton", EspOutlineSection::Skeleton },
				};
				const int outlineOptionCount = (int)(sizeof(kOutlineOptions) / sizeof(kOutlineOptions[0]));
				int outlineShownCount = 0;
				for (const auto& opt : kOutlineOptions)
					if (cfg->iEspOutlineMask & opt.bit)
						++outlineShownCount;

				char outlinePreview[32];
				if (outlineShownCount == outlineOptionCount)
					snprintf(outlinePreview, sizeof(outlinePreview), "All elements");
				else if (outlineShownCount == 0)
					snprintf(outlinePreview, sizeof(outlinePreview), "None");
				else
					snprintf(outlinePreview, sizeof(outlinePreview), "%d of %d elements", outlineShownCount, outlineOptionCount);

				ImGui::SetNextItemWidth(220.0f);
				if (ImGui::BeginCombo("Outline on", outlinePreview))
				{
					for (const auto& opt : kOutlineOptions)
					{
						bool selected = (cfg->iEspOutlineMask & opt.bit) != 0;
						if (ImGui::Checkbox(opt.label, &selected))
						{
							if (selected)
								cfg->iEspOutlineMask |= opt.bit;
							else
								cfg->iEspOutlineMask &= ~opt.bit;
						}
					}
					ImGui::Separator();
					if (ImGui::Button("All##outline", ImVec2(60, 0)))
						cfg->iEspOutlineMask = EspOutlineSection::All;
					ImGui::SameLine();
					if (ImGui::Button("None##outline", ImVec2(60, 0)))
						cfg->iEspOutlineMask = 0;
					ImGui::EndCombo();
				}
			}
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
				ImGui::ColorPicker4("##pick", cfg->colVisible, kColorPickerPopupFlags);
				ImGui::EndPopup();
			}

			if (ImGui::ColorButton("##colNotVisible", *(ImVec4*)cfg->colNotVisible))
				ImGui::OpenPopup("popup_colNotVisible");
			ImGui::SameLine();
			ImGui::Text("Not Visible");
			if (ImGui::BeginPopup("popup_colNotVisible"))
			{
				ImGui::ColorPicker4("##pick", cfg->colNotVisible, kColorPickerPopupFlags);
				ImGui::EndPopup();
			}

			if (ImGui::ColorButton("##colLines", *(ImVec4*)cfg->colLines))
				ImGui::OpenPopup("popup_colLines");
			ImGui::SameLine();
			ImGui::Text("Lines");
			if (ImGui::BeginPopup("popup_colLines"))
			{
				ImGui::ColorPicker4("##pick", cfg->colLines, kColorPickerPopupFlags);
				ImGui::EndPopup();
			}

			if (ImGui::ColorButton("##colDecoy", *(ImVec4*)cfg->colDecoy))
				ImGui::OpenPopup("popup_colDecoy");
			ImGui::SameLine();
			ImGui::Text("Decoy");
			if (ImGui::BeginPopup("popup_colDecoy"))
			{
				ImGui::ColorPicker4("##pick", cfg->colDecoy, kColorPickerPopupFlags);
				ImGui::EndPopup();
			}

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(ICON_FA_USERS " Players"))
		{
			ImGui::BeginChild("##players_list", ImVec2(0, 0), false);

			static int roleFilter = 0; // 0 all, 1 hunters, 2 survivors
			static bool sortByDistance = true;

			ImGui::SetNextItemWidth(140.0f);
			ImGui::Combo("Filter", &roleFilter, "All\0Hunters\0Survivors\0");
			ImGui::SameLine();
			ImGui::Checkbox("Sort by distance", &sortByDistance);

			// Work off a private copy so filtering/sorting never touches the live list.
			std::vector<CheatManager::PlayerInfo> players = cheat->PlayerInfos;
			if (roleFilter == 1)
				players.erase(std::remove_if(players.begin(), players.end(),
					[](const CheatManager::PlayerInfo& p) { return p.Role != 1; }), players.end());
			else if (roleFilter == 2)
				players.erase(std::remove_if(players.begin(), players.end(),
					[](const CheatManager::PlayerInfo& p) { return !p.IsSurvivor; }), players.end());
			if (sortByDistance)
				std::sort(players.begin(), players.end(),
					[](const CheatManager::PlayerInfo& a, const CheatManager::PlayerInfo& b) { return a.DistanceMeters < b.DistanceMeters; });

			int hunters = 0, survivors = 0;
			for (const auto& p : cheat->PlayerInfos)
			{
				if (p.Role == 1) hunters++;
				else if (p.IsSurvivor) survivors++;
			}
			ImGui::Text("%d players  (%d hunters, %d survivors)", (int)cheat->PlayerInfos.size(), hunters, survivors);
			if (ImGui::Button(ICON_FA_COPY " Copy all names"))
			{
				std::string all;
				for (const auto& p : players)
					all += p.Name + "\n";
				ImGui::SetClipboardText(all.c_str());
				Notify::Success("Copied player names");
			}
			ImGui::Separator();

			if (players.empty())
			{
				ImGui::TextDisabled("No players found");
			}
			else
			{
				for (int i = 0; i < (int)players.size(); i++)
				{
					const CheatManager::PlayerInfo& p = players[i];
					ImGui::PushID(i);

					if (ImGui::Button("TP"))
						cheat->RequestTeleport(p.Actor, p.Location);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Teleport to");
					ImGui::SameLine();
					if (ImGui::SmallButton(ICON_FA_COPY))
					{
						ImGui::SetClipboardText(p.Name.c_str());
						Notify::Info("Copied name");
					}
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy name");
					ImGui::SameLine();
					if (ImGui::SmallButton(ICON_FA_MASK))
					{
						cheat->RequestChangeName(p.Name);
						Notify::Info("Impersonating " + p.Name);
					}
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Impersonate (take their name)");
					if (p.IsSurvivor)
					{
						ImGui::SameLine();
						if (ImGui::SmallButton(ICON_FA_SKULL))
						{
							cheat->RequestKillSurvivor(p.Actor);
							Notify::Warn("Kill requested: " + p.Name);
						}
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("Kill survivor");
					}

					ImGui::SameLine();
					// Role tag
					if (p.Role == 1)
						ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[H]");
					else if (p.IsSurvivor)
						ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[S]");
					else
						ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[?]");
					ImGui::SameLine();
					ImGui::Text("%s", p.Name.c_str());
					ImGui::SameLine();
					ImGui::TextDisabled("%.0fm", p.DistanceMeters);
					ImGui::SameLine();
					if (p.IsVisible)
						ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), ICON_FA_EYE " Vis");
					else
						ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), ICON_FA_EYE_SLASH " Hidden");

					ImGui::PopID();
				}
			}

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(ICON_FA_SCREWDRIVER_WRENCH " Exploits"))
		{
			ImGui::BeginChild("##exploits_list", ImVec2(0, 0), false);

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

			// Master enable in menu; Magnet Key toggles bMagnetActive while enabled.
			if (ImGui::Checkbox(ICON_FA_MAGNET " Magnet", &cfg->bMagnetEnabled))
			{
				if (cfg->bMagnetEnabled)
					cfg->bMagnetActive = true;
				else
					cfg->bMagnetActive = false;
			}
			if (cfg->bMagnetEnabled)
			{
				ImGui::SameLine();
				ImGui::TextDisabled(cfg->bMagnetActive ? "(active)" : "(inactive — press key to toggle)");
			}

			// Magnet toggle rebind: click the button, then press any key, mouse
			// button, or controller button (when Controller Binds is on).
			Binds::RecorderRow("Magnet Key", cfg->iMagnetKey, cfg->bControllerBinds, true);
			if (Binds::IsPadBind(cfg->iMagnetKey) && !cfg->bControllerBinds)
				ImGui::TextDisabled("Magnet is pad-bound — enable Controller Binds in Misc");

			ImGui::Separator();
			if (ImGui::Button(ICON_FA_SKULL " Kill All Survivors + Clones"))
			{
				cheat->RequestKillAllSurvivors();
				Notify::Warn("Kill all queued (survivors + clones)");
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Kills every living survivor (KillPlayer) and pops all decoy clones (best-effort destroy).");

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
			ImGui::Text("Movement");
			ImGui::Separator();
			ImGui::Checkbox("Godmode", &cfg->bGodmode);
			ImGui::Checkbox("Speedhack", &cfg->bSpeedhack);
			if (cfg->bSpeedhack)
			{
				ImGui::PushID("speed_mult");
				ImGui::SliderFloat("Speed multiplier", &cfg->fSpeedMultiplier, 1.0f, 10.0f, "%.1fx");
				ImGui::PopID();
			}
			ImGui::Checkbox("Fly", &cfg->bFly);
			ImGui::Checkbox("Noclip", &cfg->bNoclip);
			if (cfg->bFly || cfg->bNoclip)
			{
				ImGui::PushID("fly_speed");
				ImGui::SliderFloat("Fly speed", &cfg->fFlySpeed, 200.0f, 5000.0f, "%.0f");
				ImGui::PopID();
				ImGui::TextDisabled("WASD moves, Space up, C/Ctrl down, Shift = 2x speed");
			}
			if (cfg->bNoclip)
				ImGui::TextDisabled("Noclip: no collision + WASD fly controls (Space/C up/down)");

			ImGui::Separator();
			ImGui::Text("General");
			ImGui::Separator();
			ImGui::Checkbox("Anti Server Kick", &cfg->bPreventKick);

			static char customName[64] = "";
			const float setBtnW = ImGui::CalcTextSize("Set").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - setBtnW - ImGui::GetStyle().ItemSpacing.x);
			const bool nameEntered = ImGui::InputTextWithHint("##custom_name", "Custom display name", customName, sizeof(customName), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if ((ImGui::Button("Set", ImVec2(setBtnW, 0)) || nameEntered) && customName[0] != '\0')
			{
				cheat->RequestChangeName(customName);
				Notify::Success(std::string("Name set to: ") + customName);
			}

			if (ImGui::Button(ICON_FA_RIGHT_FROM_BRACKET " Return to Main Lobby"))
				cheat->RequestReturnToMainLobby();

			ImGui::Separator();

			if (ImGui::Button("Dump Bones (Debugging)"))
				cfg->bDumpBones = true;
			ImGui::SameLine();
			if (ImGui::Button("Dump Death Flags"))
				cfg->bDumpDeath = true;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Writes each visible character's death signals to C:\\death.txt (used to debug ESP showing corpses).");

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(ICON_FA_PAINTBRUSH " Camouflage"))
		{
			ImGui::BeginChild("##camo_list", ImVec2(0, 0), false);
			if (g_camo)
				g_camo->DrawMenu();
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(ICON_FA_SLIDERS " Misc"))
		{
			ImGui::BeginChild("##misc_list", ImVec2(0, 0), false);

			ImGui::Text(ICON_FA_GAMEPAD " Controller");
			ImGui::Separator();
			if (ImGui::Checkbox("Controller Binds", &cfg->bControllerBinds) && !cfg->bControllerBinds)
				Binds::CancelRecorder();
			if (cfg->bControllerBinds)
			{
				if (!Gamepad::IsConnected())
					ImGui::TextDisabled("No controller detected");
				Binds::RecorderRow("Menu Button", cfg->iControllerMenuButton, true, false);
			}

			ImGui::Separator();
			ImGui::Text(ICON_FA_CHART_LINE " Overlays");
			ImGui::Separator();
			ImGui::Checkbox("Status HUD", &cfg->bStatusHud);
			if (cfg->bStatusHud)
			{
				ImGui::TextDisabled("Drag the HUD to move it (while this menu is open)");

				// Dropdown of HUD sections. Each entry toggles a bit in iHudMask;
				// the combo preview summarizes how many are shown.
				struct HudOption { const char* label; int bit; };
				static const HudOption kHudOptions[] = {
					{ "ESP", HudSection::Esp },
					{ "Aim / FOV", HudSection::Aim },
					{ "Magnet", HudSection::Magnet },
					{ "Survivor exploits", HudSection::Survivor },
					{ "Hunter exploits", HudSection::Hunter },
					{ "Movement", HudSection::Movement },
					{ "Combat", HudSection::Combat },
					{ "Camouflage", HudSection::Camo },
					{ "Misc", HudSection::Misc },
				};
				const int optionCount = (int)(sizeof(kHudOptions) / sizeof(kHudOptions[0]));
				int shownCount = 0;
				for (const auto& opt : kHudOptions)
					if (cfg->iHudMask & opt.bit)
						++shownCount;

				char preview[32];
				if (shownCount == optionCount)
					snprintf(preview, sizeof(preview), "All sections");
				else if (shownCount == 0)
					snprintf(preview, sizeof(preview), "None");
				else
					snprintf(preview, sizeof(preview), "%d of %d sections", shownCount, optionCount);

				ImGui::SetNextItemWidth(220.0f);
				if (ImGui::BeginCombo("HUD sections", preview))
				{
					for (const auto& opt : kHudOptions)
					{
						bool selected = (cfg->iHudMask & opt.bit) != 0;
						if (ImGui::Checkbox(opt.label, &selected))
						{
							if (selected)
								cfg->iHudMask |= opt.bit;
							else
								cfg->iHudMask &= ~opt.bit;
						}
					}
					ImGui::Separator();
					if (ImGui::Button("All", ImVec2(60, 0)))
						cfg->iHudMask = HudSection::All;
					ImGui::SameLine();
					if (ImGui::Button("None", ImVec2(60, 0)))
						cfg->iHudMask = 0;
					ImGui::EndCombo();
				}
			}
			if (ImGui::Checkbox("Notifications", &cfg->bNotifications))
				cfg->SaveSettings();
			if (ImGui::Checkbox("Streamproof menu", &cfg->bStreamproof))
				Notify::Info(cfg->bStreamproof ? "Streamproof on (cheat UI hidden from capture)" : "Streamproof off");
			ImGui::TextDisabled("Hides ESP/menu from OBS/Discord; gameplay still shows");

			ImGui::Separator();
			ImGui::Text(ICON_FA_CHART_LINE " Nameplate stats");
			ImGui::Separator();
			if (ImGui::Checkbox("Override likes & kills (server sync)", &cfg->bOverrideNameplateStats))
			{
				if (cfg->bOverrideNameplateStats)
					Notify::Info("Nameplate stat override enabled");
			}
			if (cfg->bOverrideNameplateStats)
			{
				ImGui::InputInt("Likes (thumbs up)", &cfg->iCustomLikes);
				ImGui::InputInt("Kills", &cfg->iCustomKills);
				if (cfg->iCustomLikes < 0) cfg->iCustomLikes = 0;
				if (cfg->iCustomKills < 0) cfg->iCustomKills = 0;
				ImGui::TextDisabled("Synced via server RPC — other players should see these values");
			}

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(ICON_FA_GEAR " Config"))
		{
			ImGui::BeginChild("##config_list", ImVec2(0, 0), false);

			ImGui::Text("Profiles");
			ImGui::Separator();

			static char profileName[64] = "";
			static int selectedProfile = -1;
			static std::vector<std::string> profiles;
			static bool profilesLoaded = false;
			if (!profilesLoaded)
			{
				profiles = Settings::ListProfiles();
				profilesLoaded = true;
			}

			ImGui::SetNextItemWidth(200.0f);
			ImGui::InputTextWithHint("##profilename", "new profile name", profileName, sizeof(profileName));
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save Profile") && profileName[0] != '\0')
			{
				if (cfg->SaveProfile(profileName))
				{
					if (g_camo)
						g_camo->settings.Save();
					Notify::Success(std::string("Saved profile: ") + profileName);
				}
				else
					Notify::Error("Save profile failed");
				profiles = Settings::ListProfiles();
			}

			const char* preview = (selectedProfile >= 0 && selectedProfile < (int)profiles.size())
				? profiles[selectedProfile].c_str()
				: "Select profile";
			ImGui::SetNextItemWidth(200.0f);
			if (ImGui::BeginCombo("##profilecombo", preview))
			{
				for (int i = 0; i < (int)profiles.size(); i++)
				{
					const bool sel = (i == selectedProfile);
					if (ImGui::Selectable(profiles[i].c_str(), sel))
						selectedProfile = i;
					if (sel)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			const bool haveSel = selectedProfile >= 0 && selectedProfile < (int)profiles.size();
			ImGui::BeginDisabled(!haveSel);
			if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load##profile") && haveSel)
			{
				if (cfg->LoadProfile(profiles[selectedProfile]))
					Notify::Success(std::string("Loaded profile: ") + profiles[selectedProfile]);
				else
					Notify::Error("Load failed (format mismatch)");
			}
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_TRASH_CAN " Delete##profile") && haveSel)
			{
				Settings::DeleteProfile(profiles[selectedProfile]);
				Notify::Info(std::string("Deleted profile: ") + profiles[selectedProfile]);
				profiles = Settings::ListProfiles();
				selectedProfile = -1;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Refresh"))
				profiles = Settings::ListProfiles();
			ImGui::TextDisabled("Save profile also stores camouflage settings");

			ImGui::Dummy(ImVec2(0, 8));
			ImGui::Text("Startup");
			ImGui::Separator();

			if (ImGui::Checkbox("Auto-load profile on launch", &cfg->bAutoLoadProfile))
				cfg->SaveSettings();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Loads the selected profile automatically the next time the cheat starts.");

			ImGui::BeginDisabled(!cfg->bAutoLoadProfile);
			ImGui::SetNextItemWidth(200.0f);
			const char* autoPreview = cfg->szAutoLoadProfile[0] != '\0' ? cfg->szAutoLoadProfile : "Select profile";
			if (ImGui::BeginCombo("##autoloadcombo", autoPreview))
			{
				for (int i = 0; i < (int)profiles.size(); i++)
				{
					const bool sel = profiles[i] == cfg->szAutoLoadProfile;
					if (ImGui::Selectable(profiles[i].c_str(), sel))
					{
						strncpy_s(cfg->szAutoLoadProfile, sizeof(cfg->szAutoLoadProfile),
							profiles[i].c_str(), _TRUNCATE);
						cfg->SaveSettings();
					}
					if (sel)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::EndDisabled();
			if (cfg->bAutoLoadProfile && cfg->szAutoLoadProfile[0] == '\0')
				ImGui::TextDisabled("Pick a profile above to auto-load.");

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::EndChild();

	ImGui::Separator();

	float checkboxW = ImGui::CalcTextSize("Enable").x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - checkboxW - ImGui::GetStyle().WindowPadding.x);
	ImGui::Checkbox("Enable", &cfg->bInitHooks);

	ImGui::End();
}

void Menu::DrawHud()
{
	if (!cfg || !cfg->bStatusHud)
		return;

	const int mask = cfg->iHudMask;
	auto on = [mask](int bit) { return (mask & bit) != 0; };

	// Build the list of currently-active features, honoring the section mask.
	// Keep each line terse.
	std::vector<std::string> lines;
	const bool anyEsp = cfg->bBox || cfg->bLines || cfg->bNames || cfg->bRoles ||
		cfg->bSkeleton || cfg->bDistance || cfg->bDecoys || cfg->bForceCharacterVisibility;
	if (on(HudSection::Esp) && anyEsp)
		lines.push_back(std::string(ICON_FA_EYE " ESP"));
	if (on(HudSection::Aim) && cfg->bFovChanger)
	{
		char b[48];
		snprintf(b, sizeof(b), "FOV %.0f", cfg->fFovValue);
		lines.push_back(b);
	}
	if (on(HudSection::Magnet) && cfg->bMagnetEnabled && cfg->bMagnetActive)
		lines.push_back(std::string(ICON_FA_MAGNET " Magnet [") + Binds::BindName(cfg->iMagnetKey) + "]");
	if (on(HudSection::Hunter) && cfg->bInfiniteBullets)
		lines.push_back("Infinite Bullets");
	if (on(HudSection::Hunter) && cfg->bNoGunCooldown)
		lines.push_back("No Gun Cooldown");
	if (on(HudSection::Survivor) && cfg->bSetDecoyNum)
	{
		char b[48];
		snprintf(b, sizeof(b), "Clones x%d", cfg->iDecoyCount);
		lines.push_back(b);
	}
	if (on(HudSection::Survivor) && cfg->bNoDecoyCooldown)
		lines.push_back("No Decoy Cooldown");
	if (on(HudSection::Survivor) && cfg->bAntiDetection)
		lines.push_back("Anti Detection");
	if (on(HudSection::Movement) && cfg->bGodmode)
		lines.push_back("Godmode");
	if (on(HudSection::Movement) && cfg->bSpeedhack)
	{
		char b[48];
		snprintf(b, sizeof(b), "Speedhack %.1fx", cfg->fSpeedMultiplier);
		lines.push_back(b);
	}
	if (on(HudSection::Movement) && cfg->bFly)
		lines.push_back("Fly");
	if (on(HudSection::Movement) && cfg->bNoclip)
		lines.push_back("Noclip");
	if (on(HudSection::Combat) && cfg->bAimbot)
		lines.push_back(std::string(ICON_FA_CROSSHAIRS " Aimbot [") + Binds::BindName(cfg->iAimKey) + "]");
	if (on(HudSection::Combat) && cfg->bTriggerbot)
		lines.push_back("Triggerbot");
	if (on(HudSection::Combat) && cfg->bSilentAim)
		lines.push_back("Silent Aim");
	if (on(HudSection::Combat) && cfg->bNoRecoil)
		lines.push_back("No Recoil");
	if (on(HudSection::Misc) && cfg->bPreventKick)
		lines.push_back("Anti Kick");
	if (on(HudSection::Misc) && cfg->bStreamproof)
		lines.push_back("Streamproof");
	if (on(HudSection::Misc) && cfg->bOverrideNameplateStats)
		lines.push_back("Stat override");

	if (on(HudSection::Camo) && g_camo)
	{
		const std::string camoLine = g_camo->HudStatusLine();
		if (!camoLine.empty())
			lines.push_back(camoLine);
	}

	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImGuiIO& io = ImGui::GetIO();

	const bool editable = cfg->bMenuOpen;
	const char* title = "peterhack";
	const char* dragHint = "drag";
	const ImVec2 titleSize = ImGui::CalcTextSize(title);
	float width = titleSize.x;
	// Reserve room for the "drag" hint next to the title while the HUD is editable.
	if (editable)
		width = (std::max)(width, titleSize.x + 12.0f + ImGui::CalcTextSize(dragHint).x);
	for (const auto& l : lines)
		width = (std::max)(width, ImGui::CalcTextSize(l.c_str()).x);
	width += 20.0f;

	const float lineH = ImGui::GetTextLineHeightWithSpacing();
	const float boxH = 10.0f + titleSize.y + 6.0f + (lines.empty() ? lineH : lines.size() * lineH) + 8.0f;

	// Drag to reposition — only while the menu is open, so gameplay input is
	// never eaten by the HUD. Position persists in settings.
	static bool dragging = false;
	float x = cfg->fHudPosX;
	float y = cfg->fHudPosY;
	if (editable)
	{
		const bool mouseIn = io.MousePos.x >= x && io.MousePos.x <= x + width &&
			io.MousePos.y >= y && io.MousePos.y <= y + boxH;
		if (!dragging && mouseIn && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			dragging = true;
		if (dragging)
		{
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				x += io.MouseDelta.x;
				y += io.MouseDelta.y;
			}
			else
			{
				dragging = false;
			}
		}
	}
	else
	{
		dragging = false;
	}

	// Keep the box fully on-screen, then commit the (possibly dragged) position.
	if (io.DisplaySize.x > width)
		x = (std::max)(0.0f, (std::min)(x, io.DisplaySize.x - width));
	if (io.DisplaySize.y > boxH)
		y = (std::max)(0.0f, (std::min)(y, io.DisplaySize.y - boxH));
	cfg->fHudPosX = x;
	cfg->fHudPosY = y;

	dl->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + boxH), IM_COL32(16, 16, 20, 210), 5.0f);
	// Brighter frame while draggable so it's clear the HUD can be moved.
	const ImU32 frameCol = editable ? IM_COL32(120, 180, 255, 245) : IM_COL32(64, 132, 232, 230);
	dl->AddRect(ImVec2(x, y), ImVec2(x + width, y + boxH), frameCol, 5.0f);
	dl->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + 4.0f), frameCol, 5.0f);

	float ty = y + 8.0f;
	dl->AddText(ImVec2(x + 10.0f, ty), IM_COL32(255, 255, 255, 255), title);
	if (editable)
	{
		const ImVec2 hintSize = ImGui::CalcTextSize(dragHint);
		dl->AddText(ImVec2(x + width - hintSize.x - 8.0f, ty), IM_COL32(150, 190, 255, 220), dragHint);
	}
	ty += titleSize.y + 6.0f;
	if (lines.empty())
	{
		dl->AddText(ImVec2(x + 10.0f, ty), IM_COL32(150, 150, 150, 255), "idle");
	}
	else
	{
		for (const auto& l : lines)
		{
			dl->AddText(ImVec2(x + 10.0f, ty), IM_COL32(200, 220, 255, 255), l.c_str());
			ty += lineH;
		}
	}
}
