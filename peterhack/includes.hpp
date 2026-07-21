#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <direct.h>
#include <Psapi.h>
#include <d3d12.h>
#include <dxgi1_5.h>

#pragma comment(lib, "dxgi.lib")

#include "Log.hpp"
#include "minhook/include/MinHook.h"
#include "kiero/kiero.hpp"
#include "kiero/kiero_d3d12.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"
#include "IconsFontAwesome6.h"
#include "Theme.hpp"
#include "Notify.hpp"

#include "SDK/Engine_classes.hpp"
#include "SDK/PenguinHotel_classes.hpp"
#include "SDK/BP_FirstPersonCharacter_LINK_classes.hpp"
#include "SDK/BP_FirstPersonCharacter_Main_classes.hpp"
#include "SDK/BP_FirstPersonPlayerState_Online_classes.hpp"
#include "SDK/BP_FirstPersonPlayerState_Online_parameters.hpp"
#include "SDK/BPC_CameraShake_classes.hpp"
#include "SDK/BPC_CameraShake_parameters.hpp"
#include "SDK/EnhancedInput_classes.hpp"
#include "SDK/Engine_parameters.hpp"
#include "skeleton.hpp"
#include "GamepadInput.hpp"
#include "Keybinds.hpp"
#include "CheatManager.hpp"
#include "Menu.hpp"
#include "Settings.hpp"
#include "Drawings.hpp"
#include "CamoManager.hpp"

// Set by the GatherGuard in hkProcessEvent for the duration of CheatManager::Init(). Init's many SDK
// calls - the world reads and the inline game-state mutations - internally call UObject::ProcessEvent,
// which re-enters hkProcessEvent on this same (game) thread. This flag lets the hook recognise those
// nested, self invoked calls and pass them straight through to the engine.
inline std::atomic<bool> g_inGameThreadFlush{ false };

// Set by the render thread (hkPresent) once per frame to ask the game thread to refresh the ESP
// snapshot. The game thread (hkProcessEvent) consumes it with exchange(false) and runs the world
// scan there - never on the render thread, which would race the engine's actor list and fault.
inline std::atomic<bool> g_gatherRequested{ false };

// Resolved once in hkPresent via FindGameThreadId; used by camo bridge load guards.
inline std::atomic<DWORD> g_GameThreadId{ 0 };

// Set while we intentionally call ClientReturnToMainMenu* (anti-kick must not block it).
inline std::atomic<bool> g_allowSelfReturnToMenu{ false };

// Set by the render thread each frame so the game thread can freeze/unfreeze camera look while
// the ImGui menu is open (WndProc alone is not enough for UE Enhanced Input).
inline std::atomic<bool> g_menuInputLockRequested{ false };
inline std::atomic<bool> g_menuInputLockOpen{ false };
inline std::atomic<bool> g_menuInputLockApplied{ false };

// Main global variables
inline CheatManager* cheat;
inline Menu* gui;
inline Settings* cfg;
inline CamoManager* g_camo;
inline FILE* file;
inline Drawings* draw;

// Function pointers for event handling
inline SDK::UFunction* g_fnKickLINK = nullptr;
inline SDK::UFunction* g_fnKickOnline = nullptr;
inline SDK::UFunction* g_fnClientWasKicked = nullptr;
inline SDK::UFunction* g_fnClientReturnToMainMenuWithTextReason = nullptr;
inline SDK::UFunction* g_fnOnRepBodyVisibility = nullptr;

// Death/spectate handlers on the cLeon character. Godmode blocks these when
// they target our own pawn: the kill arrives as a server->client death RPC that
// ignores the Invincible/Health fields, so the only way to survive a shot is to
// drop the death handler itself.
inline SDK::UFunction* g_fnDeathPlayer = nullptr;
inline SDK::UFunction* g_fnRagdoll = nullptr;
inline SDK::UFunction* g_fnGoToSpectate = nullptr;
inline SDK::UFunction* g_fnShowDeathWidget = nullptr;
inline SDK::UFunction* g_fnDeathEvent = nullptr;
inline SDK::UFunction* g_fnDeathUIShowAndAwait = nullptr;
inline SDK::UFunction* g_fnSpawnDeathSplash = nullptr;
inline SDK::UFunction* g_fnSetSpectatingState = nullptr;
inline SDK::UFunction* g_fnGameStateShowDeathWidget = nullptr;

// Combat ProcessEvent hooks — line/sphere trace redirect + shot effect endpoint override.
inline SDK::UFunction* g_fnLineTraceSingle = nullptr;
inline SDK::UFunction* g_fnSphereTraceSingle = nullptr;
inline SDK::UFunction* g_fnAntiChatTrace = nullptr;
inline SDK::UFunction* g_fnSpawnShotEffectServer = nullptr;
inline SDK::UFunction* g_fnSpawnShotEffectLocal = nullptr;
inline SDK::UFunction* g_fnSpawnShotEffectClient = nullptr;
inline SDK::UFunction* g_fnShakeStart = nullptr;
inline SDK::UFunction* g_fnItemShakeStart = nullptr;
inline SDK::UFunction* g_fnHunterInpActShot = nullptr;
inline SDK::UFunction* g_fnKillPlayer = nullptr;
inline SDK::UFunction* g_fnClientRestart = nullptr;

// Armed by HandleCombat on fire/trigger; consumed by hkProcessEvent trace patches.
struct CombatShotRedirect
{
	bool active = false;
	SDK::AActor* target = nullptr;
	SDK::FVector hitLocation{};
	ULONGLONG expireMs = 0;
	// Updated each gather pass; consumed when IA_Shot fires so redirect arms in sync
	// with the trace even if gather runs after the click.
	SDK::AActor* silentTarget = nullptr;
	SDK::FVector silentHit{};
	bool silentReady = false;
};
inline CombatShotRedirect g_combatRedirect{};

void InitCombatFunctionPointers();
void ForceRefreshCombatFunctionPointers();

// Local player identity for the ProcessEvent hook — pointer-only compares on the
// hot path; never touch UObjects there except in __try blocks for parm checks.
inline std::atomic<void*> g_localPawn{ nullptr };
inline std::atomic<void*> g_localPlayerState{ nullptr };
// The cLeon character body — kept even while spectating so godmode can still
// block death RPCs targeting the ragdolled corpse.
inline std::atomic<void*> g_localCharacterBody{ nullptr };

void ForceRefreshGodmodeFunctionPointers();

// Re-resolve kick/visibility UFunction* after world/server changes (BP functions are recreated).
void ForceRefreshKickFunctionPointers();
