#include "includes.hpp"

#pragma warning(disable : 4244)

#include <cmath>
#include <algorithm>
#include "SDK/EnhancedInput_classes.hpp"

#include "SDK/PenguinHotel_classes.hpp"
#include "SDK/PenguinHotel_parameters.hpp"
#include "SDK/BP_FirstPersonCharacter_cLeon_Character_classes.hpp"
#include "SDK/BP_FirstPersonCharacter_cLeon_Character_Hunter_classes.hpp"
#include "SDK/BP_FirstPersonCharacter_cLeon_Character_Hunter_parameters.hpp"
#include "SDK/BP_FirstPersonCharacter_cLeon_Character_Survivor_classes.hpp"
#include "SDK/BP_cLeonDecoy_Base_classes.hpp"
#include "SDK/BP_FirstPersonPlayerState_Online_cLeon_classes.hpp"
#include "SDK/BP_FirstPersonPlayerState_Online_cLeon_parameters.hpp"
#include "SDK/Engine_parameters.hpp"
#include "SDK/UMG_classes.hpp"
#include "SDK/UMG_parameters.hpp"
#include "SDK/ExtendedPhysicsCharacterMoverComponent_classes.hpp"

namespace
{
SDK::UClass* SafeLeonCharacterClass()
{
	__try
	{
		return SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}
}

SDK::UClass* SafeDecoyClass()
{
	__try
	{
		return SDK::ABP_cLeonDecoy_Base_C::StaticClass();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}
}

SDK::ABP_FirstPersonCharacter_cLeon_Character_C* AsLeonCharacter(SDK::ABP_FirstPersonCharacter_Main_C* main)
{
	if (!main)
		return nullptr;
	SDK::UClass* leonCls = SafeLeonCharacterClass();
	if (!leonCls || !main->IsA(leonCls))
		return nullptr;
	return static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(main);
}

void ApplyMovementExploits(SDK::ABP_FirstPersonCharacter_Main_C* baseClass);
}

#include "SDK/Mover_classes.hpp"
#include "SDK/BP_FirstPersonPlayerState_Online_parameters.hpp"

// Game window handle, owned by Main.cpp. Used to gate raw-keystate reads
// (manual flight, aim/trigger keys) to when the game is actually focused.
namespace Process
{
	extern HWND Hwnd;
}

// GAME THREAD: scan the world and publish a render-ready snapshot.
//
// Everything that touches a live UObject - GetAllActorsOfClass, the per-actor
// reads, the screen projections - happens here, on the thread the engine
// actually mutates the actor list from. Doing it on the render thread (the old
// behaviour, called straight from hkPresent) raced the game thread during
// round/level transitions and faulted deep inside the engine's
// GetAllActorsOfClass. The render thread now only draws the snapshot we publish
// at the end (see RenderEsp).
//
// All scan state lives in locals (ctx, actor/baseClass) threaded through the
// helpers, not in members, so a single invocation owns everything it touches.
// The game-thread pinning in hkProcessEvent guarantees only one thread ever
// runs this, and the GatherGuard there blocks re-entry; the local-state design
// keeps ownership explicit rather than relying on it.
namespace
{
	ImDrawList* OverlayDrawList()
	{
		if (ImDrawList* windowList = ImGui::GetWindowDrawList())
			return windowList;
		return ImGui::GetBackgroundDrawList();
	}

	void InvokeNativeProcessEvent(SDK::UObject* object, SDK::UFunction* function, void* parms)
	{
		if (!object || !function)
			return;

		const auto previousFlags = function->FunctionFlags;
		function->FunctionFlags |= 0x400;
		object->ProcessEvent(function, parms);
		function->FunctionFlags = previousFlags;
	}

	// Stronger than IsObjectValid: also rejects objects mid-teardown (RF_BeginDestroyed /
	// RF_FinishDestroyed / RF_MirroredGarbage). Calling ProcessEvent on those is what
	// crashes the game even when SEH "successfully" catches the immediate fault.
	bool IsObjectUsable(SDK::UObject* obj)
	{
		if (!obj || !CheatManager::IsObjectValid(obj))
			return false;
		__try
		{
			const auto flags = static_cast<uint32_t>(obj->Flags);
			constexpr uint32_t kDead =
				static_cast<uint32_t>(SDK::EObjectFlags::BeginDestroyed) |
				static_cast<uint32_t>(SDK::EObjectFlags::FinishDestroyed) |
				static_cast<uint32_t>(SDK::EObjectFlags::MirroredGarbage);
			if (flags & kDead)
				return false;
			if (!obj->Class)
				return false;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// Local SetMaxDecoySpawnCount only — never ServerSetMaxDecoySpawnCount.
	// The server RPC is a net ProcessEvent that faults during round/level teardown and
	// can take the process down even after we catch the AV.
	// Returns false if ProcessEvent faulted (caller should disable further RPCs).
	bool CallSetMaxDecoySpawnCountLocal(SDK::URuntimePaintableComponent* paintable, int target)
	{
		if (!paintable || !IsObjectUsable(paintable) || !paintable->Class)
			return true;

		SDK::UFunction* fnSet = paintable->Class->GetFunction("RuntimePaintableComponent", "SetMaxDecoySpawnCount");
		if (!fnSet)
			return true;

		__try
		{
			SDK::Params::RuntimePaintableComponent_SetMaxDecoySpawnCount parms{};
			parms.NewMaxDecoySpawnCount = target;
			InvokeNativeProcessEvent(paintable, fnSet, &parms);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			PhLog("[EXPLOITS:DECOY-NUM] ProcessEvent fault — skipped RPC\n");
			return false;
		}
	}
}

namespace
{
	bool SafeGetAllActorsOfClass(
		SDK::UGameplayStatics* gStatics,
		SDK::UWorld* world,
		SDK::UClass* klass,
		SDK::TArray<SDK::AActor*>* out)
	{
		if (!gStatics || !world || !klass || !out)
			return false;

		__try
		{
			gStatics->GetAllActorsOfClass(world, klass, out);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			out->Clear();
			PhLog("[peterhack] GetAllActorsOfClass fault — skipped frame\n");
			return false;
		}
	}

	bool SafeLineOfSightTo(SDK::APlayerController* pc, SDK::AActor* actor)
	{
		if (!pc || !actor || !CheatManager::IsObjectValid(pc) || !CheatManager::IsObjectValid(actor))
			return false;

		__try
		{
			return pc->LineOfSightTo(actor, SDK::FVector{}, false);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool SafeProjectWorldLocationToScreen(SDK::APlayerController* pc, const SDK::FVector& world, SDK::FVector2D& out)
	{
		if (!pc || !CheatManager::IsObjectValid(pc))
			return false;

		__try
		{
			return pc->ProjectWorldLocationToScreen(world, &out, false);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool SafeGetActorLocation(SDK::AActor* actor, SDK::FVector& out)
	{
		if (!actor || !CheatManager::IsObjectValid(actor))
			return false;

		__try
		{
			out = actor->K2_GetActorLocation();
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool SafeIsAnySimulatingPhysics(SDK::USkinnedMeshComponent* mesh)
	{
		if (!mesh || !CheatManager::IsObjectValid(mesh))
			return false;

		__try
		{
			return mesh->IsAnySimulatingPhysics();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool MeshReadyForSkeleton(SDK::USkinnedMeshComponent* mesh)
	{
		if (!mesh || !CheatManager::IsObjectValid(mesh))
			return false;

		__try
		{
			return mesh->GetNumBones() >= skeleton::None;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool IsScreenCoordValid(const SDK::FVector2D& p)
	{
		return std::isfinite(p.X) && std::isfinite(p.Y) &&
			std::fabs(p.X) < 50000.0f && std::fabs(p.Y) < 50000.0f;
	}

	void SetSimpleScreenBox(CheatManager::EspEntry& entry, const SDK::FVector2D& screen)
	{
		entry.hasBox = true;
		entry.boxMin = { screen.X - 25.0f, screen.Y - 50.0f };
		entry.boxMax = { screen.X + 25.0f, screen.Y + 10.0f };
	}

	bool SafeGetBoneScreenPos(
		SDK::USkinnedMeshComponent* mesh,
		SDK::APlayerController* pc,
		int boneIdx,
		SDK::FVector2D& out)
	{
		if (!MeshReadyForSkeleton(mesh) || boneIdx < 0 || boneIdx >= skeleton::None)
			return false;

		__try
		{
			const SDK::FVector boneLoc = mesh->GetSocketLocation(mesh->GetBoneName(boneIdx));
			return SafeProjectWorldLocationToScreen(pc, boneLoc, out) && IsScreenCoordValid(out);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool SafeGetBoneWorldPos(SDK::USkinnedMeshComponent* mesh, int boneIdx, SDK::FVector& out)
	{
		if (!MeshReadyForSkeleton(mesh) || boneIdx < 0 || boneIdx >= skeleton::None)
			return false;

		__try
		{
			out = mesh->GetSocketLocation(mesh->GetBoneName(boneIdx));
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// Control rotation of the pawn's controller (the camera/aim rotation).
	bool SafeGetControlRotationFromPawn(SDK::APawn* pawn, SDK::FRotator& out)
	{
		if (!pawn || !CheatManager::IsObjectValid(pawn))
			return false;
		__try
		{
			SDK::AController* c = pawn->Controller;
			if (!c || !CheatManager::IsObjectValid(c))
				return false;
			out = c->GetControlRotation();
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool SafeSetControlRotation(SDK::APlayerController* pc, const SDK::FRotator& rot)
	{
		if (!pc || !CheatManager::IsObjectValid(pc))
			return false;
		__try
		{
			pc->SetControlRotation(rot);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool SafeGetCameraLocation(SDK::APlayerController* pc, SDK::FVector& out)
	{
		if (!pc || !CheatManager::IsObjectValid(pc))
			return false;
		__try
		{
			SDK::APlayerCameraManager* cam = pc->PlayerCameraManager;
			if (!cam || !CheatManager::IsObjectValid(cam))
				return false;
			out = cam->GetCameraLocation();
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// Held-state check for a bind code. Pad binds have no held-state API here,
	// so they are treated as not held (aim/trigger hold keys are kb/mouse only).
	bool BindHeld(int code)
	{
		if (code == 0 || Binds::IsPadBind(code))
			return false;
		return (GetAsyncKeyState(code) & 0x8000) != 0;
	}

	bool SafeActorBeingDestroyed(SDK::AActor* actor)
	{
		if (!actor || !CheatManager::IsObjectValid(actor))
			return true;

		__try
		{
			return actor->IsActorBeingDestroyed();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return true;
		}
	}

	// Best-effort destroy of an actor (used to pop decoy/clone actors, which have
	// no server kill RPC). K2_DestroyActor is a stable native UFunction, so unlike
	// the BP-generated KillPlayer it's safe to invoke via the cached SDK wrapper.
	void SafeDestroyActor(SDK::AActor* actor)
	{
		if (!actor || !CheatManager::IsObjectValid(actor))
			return;

		__try
		{
			actor->K2_DestroyActor();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}

	bool SafeSetControllerFov(SDK::APlayerController* pc, float fov)
	{
		if (!pc || !CheatManager::IsObjectValid(pc))
			return false;

		__try
		{
			pc->FOV(fov);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool SafeDecoyMeshVisible(SDK::ABP_cLeonDecoy_Base_C* decoy)
	{
		if (!decoy || !decoy->PoseableMesh || !CheatManager::IsObjectValid(decoy->PoseableMesh))
			return false;

		__try
		{
			return decoy->PoseableMesh->bVisible;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool SafeLevelReadyForActorScan(SDK::ULevel* level)
	{
		if (!level || !CheatManager::IsObjectValid(reinterpret_cast<SDK::UObject*>(level)))
			return false;

		__try
		{
			(void)level->Actors.Num();
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
}

void CheatManager::ResetSessionState()
{
	deadActors.clear();
	forcedVisibleActors.clear();
	playerNameCache.clear();
	killAllQueue.clear();
	killAllNextMs_ = 0;
	TeleportTarget = nullptr;
	TeleportHasFallback = false;
	KillTarget = nullptr;
	bKillAllSurvivorsRequested = false;
	bChangeNameRequested = false;
	pendingChangeName.clear();
	bReturnToLobbyRequested = false;
	lastDecoyPaintable = nullptr;
	lastDecoyCountApplied = -1;
	lastDecoyCountConfigured = -1;
	lastDecoyRpcTickMs = 0;
	decoyPaintableReadyTickMs = 0;
	decoyExploitsReadyAfterMs_ = 0;
	lastSpawnedDecoyCount_ = -1;
	decoyQuiesceUntilMs_ = 0;
	decoyProcessEventDisabled_ = false;
	menuLookInputLocked = false;
	inMatchStableFrames_ = 0;
	espScansCompleted_ = 0;
	g_menuInputLockApplied.store(false, std::memory_order_release);

	EspSnapshot empty;
	{
		std::lock_guard<std::mutex> lock(snapshotMutex);
		pendingSnapshot = std::move(empty);
	}
	PlayerInfos.clear();

	if (g_camo)
		g_camo->OnSessionReset();
}

void CheatManager::ResetSpawnTransition()
{
	deadActors.clear();
	forcedVisibleActors.clear();
	playerNameCache.clear();
	inMatchStableFrames_ = 0;
	espScansCompleted_ = 0;
}

void CheatManager::Init()
{
	EspSnapshot snap;

	FrameContext ctx;
	if (!ResolveContext(ctx))
	{
		if (lastWorld_ != nullptr)
		{
			ResetSessionState();
			lastWorld_ = nullptr;
			lastPersistentLevel_ = nullptr;
			lastLocalPawn_ = nullptr;
			lastGodmodeCharacter_ = nullptr;
			g_localPawn.store(nullptr, std::memory_order_release);
			g_localPlayerState.store(nullptr, std::memory_order_release);
			g_localCharacterBody.store(nullptr, std::memory_order_release);
			wasInMatch_ = false;
			wasSpectating_ = false;
			inMatchStableFrames_ = 0;
			espScansCompleted_ = 0;
			worldStableAfterMs_ = 0;
			PhLog("[peterhack] World unavailable — session reset\n");
		}
		inMatchCached.store(false, std::memory_order_release);
		// Publish an empty snapshot so the overlay clears (rather than freezing on
		// the last frame) while we have no valid world/player - main menu, loading
		// screen, level transition.
		if (cfg->bInitHooks)
		{
			std::lock_guard<std::mutex> lock(snapshotMutex);
			pendingSnapshot = std::move(snap);
		}
		return;
	}

	snap.screenX = static_cast<float>(ctx.screenX);
	snap.screenY = static_cast<float>(ctx.screenY);

	const bool spectating = IsLocalPlayerSpectating(ctx.PlayerController);
	const bool aliveFreecam = IsAliveFreecam(ctx.PlayerController, lastGodmodeCharacter_);
	const bool inMatch = IsLocalPlayerInMatch(ctx.PlayerController) || aliveFreecam;
	// Post-death spectate still pauses ESP; alive freecam keeps the round active.
	const bool deadSpectating = spectating && !aliveFreecam;
	SDK::ULevel* level = ctx.World ? ctx.World->PersistentLevel : nullptr;
	const bool worldChanged = ctx.World != lastWorld_ || level != lastPersistentLevel_;
	const bool pawnChanged = ctx.MyPlayer != lastLocalPawn_;
	const bool leftMatch = wasInMatch_ && !inMatch;
	const bool enteredMatch = !wasInMatch_ && inMatch;
	const bool spectateEnded = wasSpectating_ && !spectating;
	const bool freecamPawnSwap =
		pawnChanged && (aliveFreecam || (wasSpectating_ && IsLiveCharacterBody(lastGodmodeCharacter_)));
	const bool freecamReturn = spectateEnded && IsLiveCharacterBody(lastGodmodeCharacter_);

	if (worldChanged || leftMatch)
	{
		ResetSessionState();
		ForceRefreshKickFunctionPointers();
	}
	else if (enteredMatch || (pawnChanged && !freecamPawnSwap) || (spectateEnded && !freecamReturn))
	{
		ResetSpawnTransition();
	}

	if (worldChanged || leftMatch || enteredMatch || (pawnChanged && !freecamPawnSwap) ||
		(spectateEnded && !freecamReturn))
	{
		if (worldChanged)
		{
			lastWorld_ = ctx.World;
			lastPersistentLevel_ = level;
			PhLog("[peterhack] World changed — session reset (%ums grace)\n", static_cast<unsigned>(kJoinGraceMs));
		}
		else if (freecamReturn || spectateEnded || enteredMatch)
		{
			PhLog("[peterhack] Spawned into match — spawn reset (%ums grace)\n", static_cast<unsigned>(kJoinGraceMs));
		}
		else if (pawnChanged)
		{
			PhLog("[peterhack] Local pawn changed — spawn reset (%ums grace)\n", static_cast<unsigned>(kJoinGraceMs));
		}
		else
		{
			PhLog("[peterhack] Left match — session reset\n");
		}

		if (worldChanged || enteredMatch || (pawnChanged && !freecamPawnSwap) ||
			(spectateEnded && !freecamReturn))
		{
			worldStableAfterMs_ = GetTickCount64() + kJoinGraceMs;
		}
	}

	lastLocalPawn_ = ctx.MyPlayer;
	if (IsObjectValid(ctx.MyPlayer) &&
		ctx.MyPlayer->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass()))
	{
		lastGodmodeCharacter_ = ctx.MyPlayer;
	}
	else if (lastGodmodeCharacter_ && !IsObjectValid(lastGodmodeCharacter_))
	{
		lastGodmodeCharacter_ = nullptr;
	}
	// Publish for the ProcessEvent hook's godmode death-RPC block (pointer-only compare).
	g_localPawn.store(ctx.MyPlayer, std::memory_order_release);
	g_localCharacterBody.store(lastGodmodeCharacter_, std::memory_order_release);
	g_localPlayerState.store(
		lastGodmodeCharacter_ && IsObjectValid(lastGodmodeCharacter_) ? lastGodmodeCharacter_->PlayerState
			: (ctx.MyPlayer && IsObjectValid(ctx.MyPlayer) ? ctx.MyPlayer->PlayerState : nullptr),
		std::memory_order_release);
	wasSpectating_ = spectating;
	wasInMatch_ = inMatch;
	inMatchCached.store(inMatch, std::memory_order_release);

	const bool joinStable = GetTickCount64() >= worldStableAfterMs_;
	if (inMatch && !deadSpectating && joinStable)
		++inMatchStableFrames_;
	else if (!aliveFreecam)
		inMatchStableFrames_ = 0;

	const bool levelReady = SafeLevelReadyForActorScan(level);
	const bool matchReady = inMatch && !deadSpectating && joinStable && levelReady &&
		inMatchStableFrames_ >= kRequiredStableFrames;

	if (matchReady && decoyExploitsReadyAfterMs_ == 0)
		decoyExploitsReadyAfterMs_ = GetTickCount64() + kDecoyExploitDelayMs;

	if (!matchReady)
	{
		if (cfg->bInitHooks)
		{
			std::lock_guard<std::mutex> lock(snapshotMutex);
			pendingSnapshot = std::move(snap);
		}
		return;
	}

	if (cfg->bFovChanger)
		SafeSetControllerFov(ctx.PlayerController, cfg->fFovValue);

	const bool needActorScan = NeedsActorScan();

	// Exploits-only path: skip the actor scan when ESP/magnet/kill tools don't need it.
	if (!needActorScan)
	{
		if (IsObjectValid(ctx.MyPlayer) &&
			ctx.MyPlayer->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass()))
		{
			auto* localChar = static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(ctx.MyPlayer);
			if (localChar && IsObjectValid(localChar))
			{
				ApplyLocalPlayerExploits(ctx, localChar);
				HandleNameplateStats(localChar);
			}
		}

		HandleChangeName(ctx.MyPlayer);
		HandleReturnToMainLobby(ctx.PlayerController);

		if (cfg->bInitHooks)
		{
			std::lock_guard<std::mutex> lock(snapshotMutex);
			pendingSnapshot = std::move(snap);
		}
		return;
	}

	if (!IsObjectValid(ctx.MyPlayer) || !IsObjectValid(ctx.World) || !IsObjectValid(ctx.PlayerController))
	{
		if (cfg->bInitHooks)
		{
			std::lock_guard<std::mutex> lock(snapshotMutex);
			pendingSnapshot = std::move(snap);
		}
		return;
	}

	SDK::FVector MyLocation{};
	if (!SafeGetActorLocation(ctx.MyPlayer, MyLocation))
	{
		if (cfg->bInitHooks)
		{
			std::lock_guard<std::mutex> lock(snapshotMutex);
			pendingSnapshot = std::move(snap);
		}
		return;
	}

	const bool espFullyWarm = IsEspFullyWarm();

	// Freecam uses the spectate pawn as MyPlayer; fall back to the live body for
	// team checks and filtering ourselves out of ESP.
	SDK::APawn* espLocalPawn = ctx.MyPlayer;
	if (lastGodmodeCharacter_ && IsObjectValid(lastGodmodeCharacter_) &&
		lastGodmodeCharacter_->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass()) &&
		(!ctx.MyPlayer ||
			!ctx.MyPlayer->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass())))
	{
		espLocalPawn = lastGodmodeCharacter_;
	}

	// get players
	SDK::TArray<SDK::AActor*> Players;
	if (!SafeGetAllActorsOfClass(ctx.GStatics, ctx.World, SDK::ABP_FirstPersonCharacter_Main_C::StaticClass(), &Players))
	{
		if (TeleportTarget || TeleportHasFallback)
			HandleTeleport(ctx.MyPlayer, {});
		if (cfg->bInitHooks)
		{
			std::lock_guard<std::mutex> lock(snapshotMutex);
			pendingSnapshot = std::move(snap);
		}
		return;
	}

	// Track which actors exist this frame so we can drop stale entries from the
	// latched dead set below - otherwise a destroyed corpse's pointer could later
	// be reused by a live actor and wrongly suppress its ESP.
	std::unordered_set<SDK::AActor*> currentActors;

	// One-shot death-flag diagnostic: start a fresh file, append one line per
	// character below, then close it out after the loop.
	if (cfg->bDumpDeath)
	{
		if (FILE* f = fopen("C:\\death.txt", "w"))
		{
			fprintf(f, "role(1=hunter,2=survivor) | Dead | ragdoll | BodyVisibility | IsLiveSelf | latchedDead\n");
			fclose(f);
		}
	}

	for (int i = 0; i < Players.Num(); i++)
	{
		if (!Players.IsValidIndex(i))
			continue;

		SDK::AActor* actor = Players[i];
		if (!actor || !IsObjectValid(actor) || SafeActorBeingDestroyed(actor))
			continue;
		if (!actor->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass()))
			continue;
		auto* baseClass = static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(actor);
		if (!baseClass)
			continue;

		currentActors.insert(actor);

		if (cfg->bDumpDeath)
			DumpDeathFlags(baseClass);

		// Skip dead/ragdolled corpses (see IsDead for why the obvious flags don't
		// work).
		if (IsDead(actor))
			continue;

		SDK::FVector Location{};
		if (!SafeGetActorLocation(actor, Location))
			continue;

		const std::string PlayerName = ResolvePlayerName(actor, baseClass);
		const bool playerVisible = SafeLineOfSightTo(ctx.PlayerController, actor);
		const bool IsVisible = espFullyWarm ? playerVisible : false;

		if (actor == espLocalPawn)
			continue;

		const int playerRole = IsHunter(baseClass) ? 1 : (IsSurvivor(baseClass) ? 2 : 0);
		const float playerDistM = MyLocation.GetDistanceToInMeters(Location);
		snap.players.push_back({ PlayerName, Location, actor, IsSurvivor(baseClass),
			playerRole, playerDistM, playerVisible });

		if (espFullyWarm)
			UpdateForcedVisibility(actor, baseClass);

		if (cfg->bDumpBones)
		{
			DumpBones(baseClass);
			cfg->bDumpBones = false;
		}

		if (cfg->bEnemyOnly && !IsEnemy(espLocalPawn, baseClass))
			continue;

		EspEntry entry;
		BuildEspEntry(ctx.PlayerController, baseClass, entry, PlayerName, Location, MyLocation, IsVisible, espFullyWarm);
		snap.entries.push_back(std::move(entry));
	}

	// Decoys ride the same entries/draw path as players (role 3). They have no
	// team, so Enemy Only never hides them - the toggle is the only gate.
	// Never abort the player scan above when decoy lookup fails.
	if (cfg->bDecoys)
	{
		SDK::UClass* decoyCls = SafeDecoyClass();
		if (decoyCls)
		{
			SDK::TArray<SDK::AActor*> Decoys;
			if (SafeGetAllActorsOfClass(ctx.GStatics, ctx.World, decoyCls, &Decoys))
			{
				for (int i = 0; i < Decoys.Num(); i++)
				{
					if (!Decoys.IsValidIndex(i))
						continue;

					SDK::AActor* actor = Decoys[i];
					if (!actor || !IsObjectValid(actor) || SafeActorBeingDestroyed(actor))
						continue;
					auto* decoy = static_cast<SDK::ABP_cLeonDecoy_Base_C*>(actor);
					if (!decoy)
						continue;

					// Skip decoys whose body is hidden - the game toggles the PoseableMesh's
					// visibility off when the decoy isn't actually showing, and an invisible
					// decoy shouldn't draw in ESP.
					if (!decoy->PoseableMesh || !IsObjectValid(decoy->PoseableMesh))
						continue;
					if (!SafeDecoyMeshVisible(decoy))
						continue;

					SDK::FVector Location{};
					if (!SafeGetActorLocation(decoy, Location))
						continue;

					EspEntry entry;
					BuildDecoyEntry(ctx.PlayerController, decoy, entry, Location, MyLocation, espFullyWarm);
					snap.entries.push_back(std::move(entry));
				}
			}
		}
	}

	if (cfg->bDumpDeath)
	{
		cfg->bDumpDeath = false;
		Beep(700, 300);
	}

	if (IsObjectValid(ctx.MyPlayer) &&
		ctx.MyPlayer->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass()))
	{
		auto* localChar = static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(ctx.MyPlayer);
		if (localChar && IsObjectValid(localChar))
		{
			const bool needMovementExploits = cfg->bGodmode || cfg->bSpeedhack || cfg->bFly || cfg->bNoclip ||
				cfg->bNoRecoil || cfg->bNoGunCooldown || cfg->bInfiniteBullets ||
				cfg->bAntiDetection || cfg->bNoDecoyCooldown || cfg->bSetDecoyNum;
			if (espFullyWarm || needMovementExploits)
				ApplyLocalPlayerExploits(ctx, localChar);
			if (!cfg->bMagnetEnabled)
				cfg->bMagnetActive = false;
			if (espFullyWarm && cfg->bMagnetEnabled && cfg->bMagnetActive && IsHunter(localChar))
				HandleMagnet(ctx.MyPlayer, ctx.MyPlayer, currentActors, MyLocation, Players, snap);
			if (espFullyWarm || cfg->bAimbot || cfg->bTriggerbot || cfg->bSilentAim)
				HandleCombat(ctx, Players);
			HandleNameplateStats(localChar);
		}
	}

	// While spectating, MyPlayer is the spectate pawn but death RPCs still hit the
	// cLeon body — keep that corpse alive/recovered when godmode is on.
	if (cfg->bGodmode && lastGodmodeCharacter_ && IsObjectValid(lastGodmodeCharacter_) &&
		lastGodmodeCharacter_->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass()) &&
		ctx.MyPlayer != lastGodmodeCharacter_)
	{
		ApplyMovementExploits(
			static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(lastGodmodeCharacter_));
	}

	// Drop dead-latch entries for actors that no longer exist (round restart,
	// corpse despawn), keeping the set bounded and preventing pointer reuse from
	// suppressing a live actor's ESP.
	for (auto it = deadActors.begin(); it != deadActors.end();)
	{
		if (currentActors.count(*it))
			++it;
		else
			it = deadActors.erase(it);
	}

	HandleTeleport(ctx.MyPlayer, currentActors);
	HandleKillTarget(ctx.MyPlayer, currentActors);
	HandleKillAllSurvivors(ctx.MyPlayer, currentActors, ctx.GStatics, ctx.World);
	if (cfg->bGodmode)
		HandleGodmodeRecovery(ctx);
	if (espFullyWarm)
	{
		HandleChangeName(ctx.MyPlayer);
		HandleReturnToMainLobby(ctx.PlayerController);
	}

	++espScansCompleted_;
	if (espScansCompleted_ == kEspWarmupScans)
		PhLog("[peterhack] ESP full detail enabled\n");

	// Publish the finished frame for the render thread to draw.
	std::lock_guard<std::mutex> lock(snapshotMutex);
	pendingSnapshot = std::move(snap);
}

// RENDER THREAD: draw the most recently published snapshot. Touches only ImGui
// and the snapshot copy - never a live UObject - so it cannot race the game
// thread's actor list. The game thread fills pendingSnapshot in Init(); we copy
// it out under the lock and render from our private copy.
void CheatManager::RenderEsp()
{
	{
		std::lock_guard<std::mutex> lock(snapshotMutex);
		drawSnapshot = pendingSnapshot;
	}

	// Hand the menu its teleport list (the same render thread reads PlayerInfos
	// right after this).
	PlayerInfos = drawSnapshot.players;

	for (const auto& entry : drawSnapshot.entries)
		DrawEntry(entry);

	// Combat FOV circle: shared by aimbot, silent aim, and triggerbot.
	if (cfg && cfg->bAimDrawFov &&
		(cfg->bAimbot || cfg->bSilentAim || cfg->bTriggerbot) &&
		drawSnapshot.screenX > 0.0f && drawSnapshot.screenY > 0.0f)
	{
		const ImU32 col = cfg->bSilentAim && !cfg->bAimbot
			? IM_COL32(120, 220, 255, 110)
			: IM_COL32(255, 255, 255, 90);
		OverlayDrawList()->AddCircle(
			ImVec2(drawSnapshot.screenX * 0.5f, drawSnapshot.screenY * 0.5f),
			cfg->fAimFov, col, 64, 1.5f);
	}

	if (drawSnapshot.magnetActive)
	{
		const float redColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
		const ImU32 colRed = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)redColor);

		const char* magnetText = "MAGNET ACTIVE";
		const ImVec2 textSize = ImGui::CalcTextSize(magnetText);
		const float textX = (drawSnapshot.screenX / 2.0f) - (textSize.x / 2.0f);
		const float textY = drawSnapshot.screenY - 30.0f;

		OverlayDrawList()->AddText(ImVec2(textX, textY), colRed,
			magnetText);
	}
}

// Walk the world -> game instance -> local player -> controller -> pawn chain,
// plus the gameplay statics, into ctx. Any null link aborts the whole frame.
bool CheatManager::ResolveContext(FrameContext& ctx)
{
	SDK::UWorld* world = SDK::UWorld::GetWorld();
	if (!world || !IsObjectValid(world))
		return false;

	SDK::UGameInstance* gameInstance = world->OwningGameInstance;
	if (!gameInstance || !IsObjectValid(gameInstance))
		return false;

	if (gameInstance->LocalPlayers.Num() <= 0)
		return false;
	SDK::ULocalPlayer* localPlayer = gameInstance->LocalPlayers[0];
	if (!localPlayer || !IsObjectValid(localPlayer))
		return false;

	if (!localPlayer->ViewportClient)
		return false;

	SDK::APlayerController* playerController = localPlayer->PlayerController;
	if (!playerController || !IsObjectValid(playerController))
		return false;

	playerController->GetViewportSize(&ctx.screenX, &ctx.screenY);

	SDK::APawn* myPlayer = playerController->K2_GetPawn();
	if (!myPlayer || !IsObjectValid(myPlayer))
		return false;

	auto* gStatics = (SDK::UGameplayStatics*)SDK::UGameplayStatics::StaticClass();
	if (!gStatics)
		return false;

	// Resolved purely as a readiness guard - the scan doesn't use it, but a null
	// here means the engine's kismet libraries aren't up yet, so we treat the
	// world as not ready.
	auto* mathLib = (SDK::UKismetMathLibrary*)SDK::UKismetMathLibrary::StaticClass();
	if (!mathLib)
		return false;

	ctx.World = world;
	ctx.PlayerController = playerController;
	ctx.MyPlayer = myPlayer;
	ctx.GStatics = gStatics;
	return true;
}

// Resolve the display name for the current actor, falling back to the
// last-known cached name.
std::string CheatManager::ResolvePlayerName(
	SDK::AActor* actor,
	SDK::ABP_FirstPersonCharacter_Main_C* baseClass)
{
	// PlayerState replicates as its own actor, independently of the pawn, so on
	// clients its pointer routinely blips to null for a frame or two even while
	// the pawn is fully valid. Don't drop the whole ESP over that - just fall
	// back to the last name we saw for this actor.
	if (!baseClass->PlayerState)
	{
		auto it = playerNameCache.find(actor);
		return it != playerNameCache.end() ? it->second : "Unknown";
	}

	// Prefer the custom in-game name (CustomPlayerName) over the platform/Steam
	// name (PlayerNamePrivate). Guard the cast with IsA in case a non-Online
	// PlayerState shows up, and fall back to the Steam name if the custom name
	// hasn't replicated in yet.
	SDK::FString* Name = &baseClass->PlayerState->PlayerNamePrivate;
	if (baseClass->PlayerState->IsA(SDK::ABP_FirstPersonPlayerState_Online_C::StaticClass()))
	{
		auto* ps = static_cast<SDK::ABP_FirstPersonPlayerState_Online_C*>(baseClass->PlayerState);
		if (ps->CustomPlayerName.IsValid())
			Name = &ps->CustomPlayerName;
	}

	if (Name->IsValid())
	{
		std::string PlayerName = Name->ToString();
		playerNameCache[actor] = PlayerName; // remember it for the null windows
		return PlayerName;
	}

	return "Unknown";
}

// Force the current actor's body visibility on/off, tracking who we touched so
// they can be restored.
void CheatManager::UpdateForcedVisibility(
	SDK::AActor* actor,
	SDK::ABP_FirstPersonCharacter_Main_C* baseClass)
{
	(void)actor;
	(void)baseClass;
	// BodyVisibility/OnRep_BodyVisibility were removed from Main on current Shipping.
}

// True when the current actor (obj/BaseClass) should be treated as a dead
// corpse and skipped.
//
// BodyVisibility can't be used here: it's reserved for the Force Character
// Visibility feature (which reveals stealthed/invisible survivors), so it can't
// double as a death flag. We latch death from two signals, either of which is
// enough to mark an actor dead for the rest of its lifetime:
//
//   1. Ragdoll - a live character (survivor or hunter) is animation-driven, so
//      its mesh isn't simulating physics; only a dead body ragdolls. This is
//      transient (the game hides the corpse and resets the ragdoll), so we
//      latch it.
//   2. The inherited `Dead` bool - authoritative when set, and it never flips
//      true for a live player. It isn't reliably replicated to observers, but
//      when it is present it catches corpses that we joined mid-round (already
//      settled/asleep ragdolls whose physics flag reads false).
//
// The latch set is pruned to live actors back in Init() to avoid a destroyed
// corpse's pointer being reused by a live actor and wrongly suppressing its ESP.
bool CheatManager::IsDead(SDK::AActor* actor)
{
	if (!actor)
		return false;
	auto* baseClass = static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(actor);
	if (!baseClass || !baseClass->Mesh)
		return false;

	const bool ragdolling = IsObjectValid(baseClass->Mesh) && SafeIsAnySimulatingPhysics(baseClass->Mesh);
	if (ragdolling || baseClass->Dead)
		deadActors.insert(actor);
	return deadActors.count(actor) > 0;
}

bool CheatManager::IsSurvivor(SDK::AActor* actor)
{
	if (!actor)
		return false;
	auto* baseClass = static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(actor);
	if (!baseClass)
		return false;
	return baseClass->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_Survivor_C::StaticClass());
}

bool CheatManager::IsSurvivor(SDK::ABP_FirstPersonCharacter_Main_C* baseClass)
{
	if (!baseClass)
		return false;
	return baseClass->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_Survivor_C::StaticClass());
}

bool CheatManager::IsHunter(SDK::AActor* actor)
{
	if (!actor)
		return false;
	auto* baseClass = static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(actor);
	if (!baseClass)
		return false;
	return baseClass->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C::StaticClass());
}

bool CheatManager::IsHunter(SDK::ABP_FirstPersonCharacter_Main_C* baseClass)
{
	if (!baseClass)
		return false;
	return baseClass->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C::StaticClass());
}

// True when the given actor is on the opposing team (survivor vs. hunter) from
// us.
bool CheatManager::IsEnemy(SDK::APawn* myPlayer, SDK::ABP_FirstPersonCharacter_Main_C* baseClass)
{
	if (!myPlayer->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass()))
		return false;
	auto* MyChar = static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(myPlayer);
	auto* myLeon = AsLeonCharacter(MyChar);
	auto* theirLeon = AsLeonCharacter(baseClass);
	if (!myLeon || !theirLeon)
		return false;
	return myLeon->IsHunter != theirLeon->IsHunter;
}

// GAME THREAD: project the current actor's skeleton (bone-pair segments) into
// screen space for the render thread to draw later. Each projection is an SDK
// call, so it has to happen here. mesh is the actor's skinned mesh (character's
// Mesh or a decoy's PoseableMesh) - both share the Leon skeleton, so the same
// bone-pair connections and box bone range apply to either.
void CheatManager::BuildSkeletonLines(
	SDK::APlayerController* pc,
	SDK::USkinnedMeshComponent* mesh,
	std::vector<std::pair<SDK::FVector2D, SDK::FVector2D>>& out)
{
	if (!MeshReadyForSkeleton(mesh))
		return;

	SDK::FVector2D BoneScreen, PrevBoneScreen;
	for (const std::pair<int, int>& Connection : skeleton::Connections)
	{
		if (SafeGetBoneScreenPos(mesh, pc, Connection.first, BoneScreen) &&
			SafeGetBoneScreenPos(mesh, pc, Connection.second, PrevBoneScreen))
		{
			out.emplace_back(BoneScreen, PrevBoneScreen);
		}
	}
}

// Build a 2D bounding box from every bone's screen position so it stays correct
// in any pose (crouch, prone, etc.). Returns false when no bone projected
// on-screen.
bool CheatManager::ComputeBoundingBox(
	SDK::APlayerController* pc,
	SDK::USkinnedMeshComponent* mesh,
	SDK::FVector2D& BoxMin,
	SDK::FVector2D& BoxMax)
{
	if (!MeshReadyForSkeleton(mesh))
		return false;

	bool bHasBox = false;
	for (int BoneIdx = skeleton::amm; BoneIdx < skeleton::None; BoneIdx++)
	{
		SDK::FVector2D BoneScreenPos;
		if (!SafeGetBoneScreenPos(mesh, pc, BoneIdx, BoneScreenPos))
			continue;

		if (!bHasBox)
		{
			BoxMin = BoxMax = BoneScreenPos;
			bHasBox = true;
			continue;
		}

		if (BoneScreenPos.X < BoxMin.X)
			BoxMin.X = BoneScreenPos.X;
		if (BoneScreenPos.Y < BoxMin.Y)
			BoxMin.Y = BoneScreenPos.Y;
		if (BoneScreenPos.X > BoxMax.X)
			BoxMax.X = BoneScreenPos.X;
		if (BoneScreenPos.Y > BoxMax.Y)
			BoxMax.Y = BoneScreenPos.Y;
	}
	return bHasBox;
}

// GAME THREAD: project the current actor's world state (role, distance,
// skeleton, box, snapline) into a render-ready EspEntry. All SDK/UObject access
// for one player's overlay lives here.
void CheatManager::BuildEspEntry(
	SDK::APlayerController* pc,
	SDK::ABP_FirstPersonCharacter_Main_C* baseClass,
	EspEntry& entry,
	const std::string& PlayerName,
	SDK::FVector Location,
	SDK::FVector MyLocation,
	bool IsVisible,
	bool fullMeshDetail)
{
	entry.name = PlayerName;
	entry.isVisible = IsVisible;
	if (IsHunter(baseClass))
		entry.role = 1;
	else if (IsSurvivor(baseClass))
		entry.role = 2;
	else
		entry.role = 0;
	entry.distanceMeters = MyLocation.GetDistanceToInMeters(Location);

	if (entry.role == 1 && IsHunter(baseClass) && IsObjectValid(baseClass))
	{
		if (baseClass->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C::StaticClass()))
			entry.ammo = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C*>(baseClass)->CurrentBullet;
	}

	if (fullMeshDetail && baseClass && baseClass->Mesh && MeshReadyForSkeleton(baseClass->Mesh))
	{
		if (cfg->bSkeleton)
			BuildSkeletonLines(pc, baseClass->Mesh, entry.skeletonLines);

		entry.hasBox = ComputeBoundingBox(pc, baseClass->Mesh, entry.boxMin, entry.boxMax);
	}
	else
	{
		SDK::FVector2D screen;
		if (SafeProjectWorldLocationToScreen(pc, Location, screen) && IsScreenCoordValid(screen))
			SetSimpleScreenBox(entry, screen);
	}

	if (cfg->bLines)
	{
		SDK::FVector2D Screen;
		if (SafeProjectWorldLocationToScreen(pc, Location, Screen) && IsScreenCoordValid(Screen))
		{
			entry.hasSnapline = true;
			entry.snaplineScreen = Screen;
		}
	}
}

// GAME THREAD: like BuildEspEntry, but for a decoy. A decoy is a frozen
// PoseableMesh copy of the Leon skeleton with no PlayerState/team - so no name
// lookup, role test or visibility; it's always drawn in the "visible" colour
// and labelled "Decoy".
void CheatManager::BuildDecoyEntry(SDK::APlayerController* pc,
	SDK::AActor* decoyActor,
	EspEntry& entry,
	SDK::FVector Location,
	SDK::FVector MyLocation,
	bool fullMeshDetail)
{
	auto* decoy = decoyActor ? decoyActor->IsA(SDK::ABP_cLeonDecoy_Base_C::StaticClass())
		? static_cast<SDK::ABP_cLeonDecoy_Base_C*>(decoyActor)
		: nullptr : nullptr;
	if (!decoy)
		return;
	entry.name = "Decoy";
	entry.isVisible = true;
	entry.role = 3;
	entry.distanceMeters = MyLocation.GetDistanceToInMeters(Location);

	if (fullMeshDetail && decoy->PoseableMesh && MeshReadyForSkeleton(decoy->PoseableMesh))
	{
		if (cfg->bSkeleton)
			BuildSkeletonLines(pc, decoy->PoseableMesh, entry.skeletonLines);

		entry.hasBox = ComputeBoundingBox(pc, decoy->PoseableMesh, entry.boxMin, entry.boxMax);
	}
	else
	{
		SDK::FVector2D screen;
		if (SafeProjectWorldLocationToScreen(pc, Location, screen) && IsScreenCoordValid(screen))
			SetSimpleScreenBox(entry, screen);
	}

	if (cfg->bLines)
	{
		SDK::FVector2D Screen;
		if (SafeProjectWorldLocationToScreen(pc, Location, Screen) && IsScreenCoordValid(Screen))
		{
			entry.hasSnapline = true;
			entry.snaplineScreen = Screen;
		}
	}
}

// RENDER THREAD: draw the enabled ESP overlays (skeleton, box, name, role,
// distance, snapline) for one prebuilt entry. ImGui only - no SDK calls, no
// UObject access (everything was resolved in BuildEspEntry on the game thread).
void CheatManager::DrawEntry(const EspEntry& entry)
{
	ImDrawList* const dl = OverlayDrawList();
	const bool outlineEnabled = cfg->bEspOutline;
	const int outlineMask = cfg->iEspOutlineMask;
	const float outlineThickness = cfg->fEspOutlineThickness;
	const ImU32 colOutline = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)cfg->colEspOutline);
	const auto wantOutline = [&](int bit) -> bool
	{
		return outlineEnabled && (outlineMask & bit);
	};

	const ImU32 colEsp = ImGui::ColorConvertFloat4ToU32(
		entry.role == 3 ? *(ImVec4*)cfg->colDecoy
		: entry.isVisible ? *(ImVec4*)cfg->colVisible
		: *(ImVec4*)cfg->colNotVisible);
	const ImU32 colLine = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)cfg->colLines);

	const float fff[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	const ImU32 colWhite = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)fff);

	if (cfg->bSkeleton)
	{
		for (const auto& seg : entry.skeletonLines)
		{
			if (!IsScreenCoordValid(seg.first) || !IsScreenCoordValid(seg.second))
				continue;
			Drawings::DrawLine(dl,
				ImVec2(seg.first.X, seg.first.Y),
				ImVec2(seg.second.X, seg.second.Y),
				colEsp, 1.0f,
				wantOutline(EspOutlineSection::Skeleton), colOutline, outlineThickness);
		}
	}

	if (entry.hasBox && IsScreenCoordValid(entry.boxMin) && IsScreenCoordValid(entry.boxMax))
	{
		if (cfg->bNames)
		{
			Drawings::DrawText(dl, ImVec2(entry.boxMin.X, entry.boxMin.Y - 15),
				colEsp, entry.name.c_str(),
				wantOutline(EspOutlineSection::Name), colOutline, outlineThickness);
		}

		if (cfg->bRoles)
		{
			const char* roleText;
			if (entry.role == 1)
				roleText = "Hunter";
			else if (entry.role == 2)
				roleText = "Survivor";
			else if (entry.role == 3)
				roleText = "Decoy";
			else
				roleText = nullptr;

			if (roleText)
			{
				const float nameWidth = cfg->bNames ? ImGui::CalcTextSize(entry.name.c_str()).x + 5 : 0.0f;
				Drawings::DrawText(dl, ImVec2(entry.boxMin.X + nameWidth, entry.boxMin.Y - 15),
					colWhite, roleText,
					wantOutline(EspOutlineSection::Role), colOutline, outlineThickness);
			}
		}

		if (cfg->bBox)
		{
			draw->DrawBox(dl,
				entry.boxMin.X, entry.boxMin.Y,
				entry.boxMax.X - entry.boxMin.X, entry.boxMax.Y - entry.boxMin.Y,
				colEsp, 1,
				wantOutline(EspOutlineSection::Box), colOutline, outlineThickness);
		}

		if (cfg->bDistance)
		{
			char DistanceText[32];
			snprintf(DistanceText, sizeof(DistanceText), "%.0fm", entry.distanceMeters);

			const ImVec2 TextSize = ImGui::CalcTextSize(DistanceText);
			const float TextX = (entry.boxMin.X + entry.boxMax.X) * 0.5f - TextSize.x * 0.5f;
			Drawings::DrawText(dl, ImVec2(TextX, entry.boxMax.Y + 2),
				colEsp, DistanceText,
				wantOutline(EspOutlineSection::Distance), colOutline, outlineThickness);
		}

		// if (cfg->bHunterAmmo && entry.ammo >= 0)
		// {
		// 	char AmmoText[32];
		// 	snprintf(AmmoText, sizeof(AmmoText), "Ammo: %d", entry.ammo);

		// 	const ImVec2 TextSize = ImGui::CalcTextSize(AmmoText);
		// 	const float TextX = (entry.boxMin.X + entry.boxMax.X) * 0.5f -
		// TextSize.x * 0.5f; 	const float TextY = entry.boxMax.Y + 2 +
		// (cfg->bDistance ? ImGui::GetTextLineHeight() : 0.0f);
		// 	Drawings::DrawText(dl, ImVec2(TextX, TextY), colWhite, AmmoText, outline, colOutline);
		// }
	}

	if (cfg->bLines && entry.hasSnapline && IsScreenCoordValid(entry.snaplineScreen))
	{
		const auto& io = ImGui::GetIO();
		Drawings::DrawLine(dl,
			ImVec2(static_cast<float>(io.DisplaySize.x / 2), static_cast<float>(io.DisplaySize.y)),
			ImVec2(entry.snaplineScreen.X, entry.snaplineScreen.Y),
			colLine, 0.7f,
			wantOutline(EspOutlineSection::Lines), colOutline, outlineThickness);
	}
}

// Teleport us onto the requested actor, then clear the request. The target is
// resolved by actor pointer rather than a PlayerInfos index, since that list
// (and the dead-player latch that filters it) is rebuilt every frame and a
// captured index can drift to the wrong entry or go out of range by the time
// this runs. currentActors confirms the actor still exists this frame before we
// use it.
void CheatManager::HandleTeleport(
	SDK::APawn* myPlayer,
	const std::unordered_set<SDK::AActor*>& currentActors)
{
	SDK::FVector dest{};
	bool haveDest = false;

	if (TeleportTarget && currentActors.count(TeleportTarget) && IsObjectValid(TeleportTarget))
	{
		dest = TeleportTarget->K2_GetActorLocation();
		haveDest = true;
	}
	else if (TeleportHasFallback)
	{
		dest = TeleportFallbackLocation;
		haveDest = true;
	}

	if (haveDest && myPlayer && IsObjectValid(myPlayer))
	{
		const SDK::FRotator currentRotation = myPlayer->K2_GetActorRotation();
		myPlayer->K2_TeleportTo(dest, currentRotation);
	}

	TeleportTarget = nullptr;
	TeleportHasFallback = false;
}

void CheatManager::RequestTeleport(SDK::AActor* actor, const SDK::FVector& fallbackLocation)
{
	TeleportTarget = actor;
	TeleportFallbackLocation = fallbackLocation;
	TeleportHasFallback = true;
}

void CheatManager::HandleMagnet(
	SDK::APawn* myPlayer,
	SDK::AActor* selfActor,
	const std::unordered_set<SDK::AActor*>& currentActors,
	const SDK::FVector& MyLocation,
	SDK::TArray<SDK::AActor*>& Players,
	EspSnapshot& snap)
{
	// Flag the overlay so the render thread draws the "MAGNET ACTIVE" banner
	// (ImGui can't run here on the game thread).
	snap.magnetActive = true;

	// Get the player's forward direction from their rotation
	SDK::FVector ForwardDirection = myPlayer->GetActorForwardVector();
	ForwardDirection.Normalize();

	// Magnet effect: pull all other players in front of the local player's view
	int depthIndex = 0;
	for (int j = 0; j < Players.Num(); j++)
	{
		if (!Players.IsValidIndex(j))
			continue;

		SDK::AActor* otherActor = Players[j];
		if (!otherActor || otherActor == selfActor)
			continue;

		SDK::ABP_FirstPersonCharacter_Main_C* otherBaseClass = (SDK::ABP_FirstPersonCharacter_Main_C*)otherActor;
		if (!otherBaseClass)
			continue;

		// Skip dead
		if (IsDead(otherActor))
			continue;

		// Only pull survivors
		if (!IsSurvivor(otherBaseClass))
			continue;

		// Spread players in depth to prevent stacking
		float depthSpread = depthIndex * 120.0f;
		SDK::FVector targetPosition = MyLocation + ForwardDirection * (150.0f + depthSpread);
		if (IsObjectValid(otherBaseClass))
			otherBaseClass->K2_SetActorLocation(targetPosition, false, nullptr, true);
		++depthIndex;
	}
}

void CheatManager::KillSurvivor(SDK::APawn* myPlayer, SDK::AActor* actor)
{
	if (!myPlayer || !actor || myPlayer == actor || !IsHunter(myPlayer) || !IsSurvivor(actor) || IsDead(actor))
		return;
	if (actor->IsA(SDK::ABP_cLeonDecoy_Base_C::StaticClass()))
		return;

	auto* hunter = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C*>(myPlayer);
	auto* survivor = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_Survivor_C*>(actor);

	if (!IsObjectValid(hunter) || !IsObjectValid(survivor))
		return;

	// Resolve the function fresh from the hunter's current class at call time and
	// invoke it directly, instead of the SDK wrapper's cached-once static
	// UFunction* (see KillPlayer in the generated functions.cpp). The engine
	// recreates BP-generated functions between rounds, so that static dangles
	// after a round transition and ProcessEvent then walks a freed function's
	// garbage parameter layout - faulting with a write AV deep inside the engine.
	SDK::UFunction* fn = hunter->Class->GetFunction("BP_FirstPersonCharacter_cLeon_Character_Hunter_C", "KillPlayer");
	if (!fn)
		return;

	SDK::Params::BP_FirstPersonCharacter_cLeon_Character_Hunter_C_KillPlayer parms{};
	parms.FirstpersonCharacter = survivor;
	parms.SourcePlayerState = hunter->LastMyPlayerState;
	hunter->ProcessEvent(fn, &parms);
}

void CheatManager::ExecuteSilentAimKill(SDK::APawn* hunter, SDK::AActor* target)
{
	KillSurvivor(hunter, target);
}

// Kill a single requested survivor. Like HandleTeleport, the target is resolved
// by actor pointer and confirmed against currentActors before use, since the
// snapshot/dead-latch is rebuilt every frame. KillSurvivor itself re-validates
// role/liveness, so a wrong pick (e.g. a hunter) is just a no-op.
void CheatManager::HandleKillTarget(
	SDK::APawn* myPlayer,
	const std::unordered_set<SDK::AActor*>& currentActors)
{
	if (KillTarget && currentActors.count(KillTarget))
		KillSurvivor(myPlayer, KillTarget);
	KillTarget = nullptr;
}

void CheatManager::HandleKillAllSurvivors(
	SDK::APawn* myPlayer,
	const std::unordered_set<SDK::AActor*>& currentActors,
	SDK::UGameplayStatics* gStatics,
	SDK::UWorld* world)
{
	auto isDecoy = [&](SDK::AActor* actor) -> bool {
		return actor && IsObjectValid(actor) &&
			actor->IsA(SDK::ABP_cLeonDecoy_Base_C::StaticClass());
	};

	// A real living survivor pawn (not us, not a hunter, not a corpse, not a clone).
	auto isValidSurvivorTarget = [&](SDK::AActor* actor) -> bool {
		if (!actor || actor == myPlayer || !currentActors.count(actor) || !IsObjectValid(actor))
			return false;
		if (actor->IsA(SDK::ABP_cLeonDecoy_Base_C::StaticClass()))
			return false;
		if (!IsSurvivor(actor) || IsDead(actor))
			return false;
		return true;
	};

	// A live decoy/clone actor. Decoys aren't in currentActors (they ride their own
	// enumeration), so validate them directly.
	auto isValidDecoyTarget = [&](SDK::AActor* actor) -> bool {
		return isDecoy(actor) && !SafeActorBeingDestroyed(actor);
	};

	// A fresh request seeds the pending set with living survivors AND every live
	// clone. Kills/destroys are paced below so ProcessEvent isn't spammed.
	if (bKillAllSurvivorsRequested)
	{
		bKillAllSurvivorsRequested = false;
		killAllQueue.clear();

		size_t survivorCount = 0;
		for (SDK::AActor* actor : currentActors)
		{
			if (isValidSurvivorTarget(actor))
			{
				killAllQueue.insert(actor);
				++survivorCount;
			}
		}

		// Enumerate decoys independently of the Decoy ESP toggle so kill-all always
		// clears clones too.
		size_t decoyCount = 0;
		SDK::TArray<SDK::AActor*> Decoys;
		if (SafeGetAllActorsOfClass(gStatics, world, SDK::ABP_cLeonDecoy_Base_C::StaticClass(), &Decoys))
		{
			for (int i = 0; i < Decoys.Num(); i++)
			{
				if (!Decoys.IsValidIndex(i))
					continue;
				SDK::AActor* actor = Decoys[i];
				if (isValidDecoyTarget(actor) && killAllQueue.insert(actor).second)
					++decoyCount;
			}
		}

		killAllNextMs_ = 0;
		PhLog("[KILL-ALL] Queued %zu survivors + %zu clones (%ums spacing)\n",
			survivorCount, decoyCount, static_cast<unsigned>(kKillAllIntervalMs));
	}

	if (killAllQueue.empty())
		return;

	const ULONGLONG now = GetTickCount64();
	if (now < killAllNextMs_)
		return;

	for (auto it = killAllQueue.begin(); it != killAllQueue.end();)
	{
		SDK::AActor* actor = *it;
		const bool decoy = isDecoy(actor);
		const bool valid = decoy ? isValidDecoyTarget(actor) : isValidSurvivorTarget(actor);
		if (!valid)
		{
			it = killAllQueue.erase(it);
			continue;
		}

		// Decoys have no server kill RPC — pop them via K2_DestroyActor (best-effort;
		// removes the clone at least locally). Real survivors go through KillPlayer.
		if (decoy)
			SafeDestroyActor(actor);
		else
			KillSurvivor(myPlayer, actor);

		killAllQueue.erase(it);
		killAllNextMs_ = now + kKillAllIntervalMs;
		break;
	}
}

void CheatManager::HandleChangeName(SDK::APawn* myPlayer)
{
	if (!bChangeNameRequested)
		return;
	bChangeNameRequested = false;

	std::string name = std::move(pendingChangeName);
	pendingChangeName.clear();

	if (name.empty() || !myPlayer || !IsObjectValid(myPlayer))
		return;

	auto* myChar = static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(myPlayer);
	if (!myChar)
		return;

	auto* playerState = myChar->PlayerState;
	if (!playerState || !IsObjectValid(playerState))
		return;

	if (!playerState->IsA(SDK::ABP_FirstPersonPlayerState_Online_C::StaticClass()))
		return;

	auto* onlineState = static_cast<SDK::ABP_FirstPersonPlayerState_Online_C*>(playerState);

	SDK::UFunction* fn = onlineState->Class->GetFunction("BP_FirstPersonPlayerState_Online_C", "SetName(Server)");
	if (!fn)
		return;

	// Names come through as UTF-8 (FString::ToString); widen properly so
	// non-ASCII names survive.
	std::wstring wname;
	int wlen = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
	if (wlen > 1)
	{
		wname.resize(wlen - 1); // drop the counted null terminator from the string's length
		MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, &wname[0], wlen);
	}

	SDK::Params::BP_FirstPersonPlayerState_Online_C_SetName_Server_ parms{};
	parms.CustomPlayerName_0 = SDK::FString(wname.c_str());
	onlineState->ProcessEvent(fn, &parms);
}

// GAME THREAD: push custom likes (EEYAN) and kills (ME) through the replicated
// PlayerState server RPCs so the floating nameplate updates for everyone.
void CheatManager::HandleNameplateStats(SDK::ABP_FirstPersonCharacter_Main_C* baseClass)
{
	static int s_lastWantLikes = INT32_MIN;
	static int s_lastWantKills = INT32_MIN;
	static ULONGLONG s_lastSyncMs = 0;

	if (!cfg || !baseClass || !IsObjectValid(baseClass))
		return;

	if (!cfg->bOverrideNameplateStats)
	{
		s_lastWantLikes = INT32_MIN;
		s_lastWantKills = INT32_MIN;
		s_lastSyncMs = 0;
		return;
	}

	if (!inMatchCached.load(std::memory_order_acquire))
		return;

	auto* playerState = baseClass->PlayerState;
	if (!playerState || !IsObjectValid(playerState))
		return;
	if (!playerState->IsA(SDK::ABP_FirstPersonPlayerState_Online_cLeon_C::StaticClass()))
		return;

	auto* leonPs = static_cast<SDK::ABP_FirstPersonPlayerState_Online_cLeon_C*>(playerState);
	if (!leonPs->Class)
		return;

	const int wantLikes = cfg->iCustomLikes < 0 ? 0 : cfg->iCustomLikes;
	const int wantKills = cfg->iCustomKills < 0 ? 0 : cfg->iCustomKills;

	const ULONGLONG now = GetTickCount64();
	const bool configChanged = wantLikes != s_lastWantLikes || wantKills != s_lastWantKills;
	const bool stale = (now - s_lastSyncMs) >= 3000;

	// Do not re-sync every frame when server replication drifts — that was calling
	// Update*Point(Local) ~60 Hz and faulting inside ProcessEvent.
	if (!configChanged && !stale)
		return;

	s_lastWantLikes = wantLikes;
	s_lastWantKills = wantKills;
	s_lastSyncMs = now;

	__try
	{
		if (SDK::UFunction* fn = leonPs->Class->GetFunction(
				"BP_FirstPersonPlayerState_Online_cLeon_C", "UpdateEEYANPoint(Server)"))
		{
			SDK::Params::BP_FirstPersonPlayerState_Online_cLeon_C_UpdateEEYANPoint_Server_ parms{};
			parms.CurrentEEYAN_Point = wantLikes;
			leonPs->ProcessEvent(fn, &parms);
		}

		if (SDK::UFunction* fn = leonPs->Class->GetFunction(
				"BP_FirstPersonPlayerState_Online_cLeon_C", "UpdateMEPoint(Server)"))
		{
			SDK::Params::BP_FirstPersonPlayerState_Online_cLeon_C_UpdateMEPoint_Server_ parms{};
			parms.CurrentME_Point = wantKills;
			leonPs->ProcessEvent(fn, &parms);
		}

	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}

// True if Obj is still a fully-live object. The scan and its mutations run on
// the game thread, but an SDK call earlier in the same scan can destroy/GC an
// actor, so re-check right before touching one.
bool CheatManager::IsObjectValid(SDK::UObject* Obj)
{
	if (!Obj || Obj->Index < 0)
		return false;
	if (SDK::UObject::GObjects->GetByIndex(Obj->Index) != Obj)
		return false;
	return true;
}

void CheatManager::DumpBones(
	SDK::ABP_FirstPersonCharacter_Main_C* baseClass)
{
	// Guard the whole pointer chain - any of these can be null on
	// proxies/streaming actors.
	if (!baseClass || !baseClass->Mesh || !baseClass->Mesh->SkeletalMesh || !baseClass->Mesh->SkeletalMesh->Skeleton)
		return;

	FILE* Log = fopen("C:\\bones.txt", "w");

	if (Log)
	{
		auto meshname = baseClass->Mesh->SkeletalMesh->Name;
		auto bonetree = baseClass->Mesh->SkeletalMesh->Skeleton->BoneTree;
		for (int i = 0; i < bonetree.Num(); i++)
		{
			auto boneName = baseClass->Mesh->GetBoneName(i);

			fprintf(Log, "%s = %d,\n", boneName.GetRawString().c_str(), i);
		}

		fclose(Log);
		Beep(500, 500);
	}
	else
	{
		PhLog("Failed to open file for writing bones.\n");
	}
}

// Appends one line describing this character's death-related signals. Used to
// diagnose ESP drawing corpses: run "Dump Death Flags" from Misc while looking
// at the offending body, then read C:\death.txt to see which flag distinguishes
// a corpse from a live player.
void CheatManager::DumpDeathFlags(
	SDK::ABP_FirstPersonCharacter_Main_C* baseClass)
{
	if (!baseClass)
		return;

	FILE* f = fopen("C:\\death.txt", "a");
	if (!f)
		return;

	const int role = IsHunter(baseClass) ? 1 : (IsSurvivor(baseClass) ? 2 : 0);
	const bool ragdoll = baseClass->Mesh && IsObjectValid(baseClass->Mesh) &&
		SafeIsAnySimulatingPhysics(baseClass->Mesh);
	const bool dead = baseClass->Dead;
	const bool spectating = baseClass->IsSpectating;
	const bool latched = deadActors.count(static_cast<SDK::AActor*>(baseClass)) > 0;

	fprintf(f, "role=%d Dead=%d ragdoll=%d IsSpectating=%d latchedDead=%d\n",
		role, (int)dead, (int)ragdoll, (int)spectating, (int)latched);
	fclose(f);
}

void CheatManager::ApplyMenuInputLock(bool menuOpen)
{
	if (!menuOpen && !menuLookInputLocked && !g_menuInputLockApplied.load(std::memory_order_acquire))
		return;

	SDK::UWorld* world = SDK::UWorld::GetWorld();
	if (!world || !world->OwningGameInstance)
		return;
	if (world->OwningGameInstance->LocalPlayers.Num() <= 0)
		return;

	SDK::ULocalPlayer* localPlayer = world->OwningGameInstance->LocalPlayers[0];
	if (!localPlayer)
		return;

	SDK::APlayerController* playerController = localPlayer->PlayerController;
	if (!playerController || !IsObjectValid(playerController))
		return;

	if (menuOpen)
	{
		static SDK::UFunction* fnGameAndUI = nullptr;
		if (!fnGameAndUI)
			fnGameAndUI = SDK::UWidgetBlueprintLibrary::StaticClass()->GetFunction("WidgetBlueprintLibrary", "SetInputMode_GameAndUIEx");
		if (fnGameAndUI)
		{
			SDK::Params::WidgetBlueprintLibrary_SetInputMode_GameAndUIEx parms{};
			parms.PlayerController = playerController;
			parms.InWidgetToFocus = nullptr;
			parms.InMouseLockMode = SDK::EMouseLockMode::DoNotLock;
			parms.bHideCursorDuringCapture = false;
			parms.bFlushInput = true;
			SDK::UWidgetBlueprintLibrary::GetDefaultObj()->ProcessEvent(fnGameAndUI, &parms);
		}

		SDK::UFunction* fn = playerController->Class->GetFunction("Controller", "SetIgnoreLookInput");
		if (fn)
		{
			SDK::Params::Controller_SetIgnoreLookInput parms{};
			parms.bNewLookInput = true;
			playerController->ProcessEvent(fn, &parms);
		}

		playerController->bShowMouseCursor = true;
		menuLookInputLocked = true;
		g_menuInputLockApplied.store(true, std::memory_order_release);
		return;
	}

	const bool inMatch = IsLocalPlayerInMatch(playerController);

	static SDK::UFunction* fnGameOnly = nullptr;
	static SDK::UFunction* fnGameAndUI = nullptr;
	if (!fnGameOnly)
		fnGameOnly = SDK::UWidgetBlueprintLibrary::StaticClass()->GetFunction("WidgetBlueprintLibrary", "SetInputMode_GameOnly");
	if (!fnGameAndUI)
		fnGameAndUI = SDK::UWidgetBlueprintLibrary::StaticClass()->GetFunction("WidgetBlueprintLibrary", "SetInputMode_GameAndUIEx");

	if (inMatch)
	{
		if (fnGameOnly)
		{
			SDK::Params::WidgetBlueprintLibrary_SetInputMode_GameOnly parms{};
			parms.PlayerController = playerController;
			parms.bFlushInput = false;
			SDK::UWidgetBlueprintLibrary::GetDefaultObj()->ProcessEvent(fnGameOnly, &parms);
		}
		playerController->bShowMouseCursor = false;
	}
	else if (fnGameAndUI)
	{
		SDK::Params::WidgetBlueprintLibrary_SetInputMode_GameAndUIEx parms{};
		parms.PlayerController = playerController;
		parms.InWidgetToFocus = nullptr;
		parms.InMouseLockMode = SDK::EMouseLockMode::DoNotLock;
		parms.bHideCursorDuringCapture = false;
		parms.bFlushInput = false;
		SDK::UWidgetBlueprintLibrary::GetDefaultObj()->ProcessEvent(fnGameAndUI, &parms);
		playerController->bShowMouseCursor = true;
	}

	SDK::UFunction* fnReset = playerController->Class->GetFunction("Controller", "ResetIgnoreLookInput");
	if (fnReset)
		playerController->ProcessEvent(fnReset, nullptr);

	menuLookInputLocked = false;
	g_menuInputLockApplied.store(false, std::memory_order_release);
}

bool CheatManager::IsLocalPlayerSpectating(SDK::APlayerController* playerController)
{
	if (!playerController || !IsObjectValid(playerController))
		return false;

	__try
	{
		SDK::APawn* pawn = playerController->K2_GetPawn();
		if (!pawn || !IsObjectValid(pawn))
			return false;

		return pawn->IsA(SDK::ASpectatorPawn::StaticClass()) ||
			(pawn->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass()) &&
				static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(pawn)->IsSpectating);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

bool CheatManager::IsLocalPlayerInMatch(SDK::APlayerController* playerController)
{
	if (!playerController || !IsObjectValid(playerController))
		return false;

	__try
	{
		SDK::APawn* pawn = playerController->K2_GetPawn();
		if (!pawn || !IsObjectValid(pawn))
			return false;

		return pawn->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass());
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

bool CheatManager::IsLiveCharacterBody(SDK::AActor* body)
{
	if (!body || !IsObjectValid(body))
		return false;
	if (!body->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass()))
		return false;

	auto* mainChar = static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(body);
	__try
	{
		return !mainChar->Dead && !mainChar->IsSpectating;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

bool CheatManager::IsAliveFreecam(SDK::APlayerController* playerController, SDK::AActor* cachedBody)
{
	if (!IsLocalPlayerSpectating(playerController))
		return false;
	return IsLiveCharacterBody(cachedBody);
}

bool CheatManager::NeedsEspDraw() const
{
	if (!cfg)
		return false;
	return cfg->bNames || cfg->bRoles || cfg->bBox || cfg->bSkeleton ||
		cfg->bLines || cfg->bDistance || cfg->bHunterAmmo;
}

bool CheatManager::NeedsActorScan() const
{
	if (!cfg)
		return false;
	return NeedsEspDraw() || cfg->bDecoys || (cfg->bMagnetEnabled && cfg->bMagnetActive) ||
		cfg->bForceCharacterVisibility ||
		cfg->bAimbot || cfg->bTriggerbot || cfg->bSilentAim ||
		TeleportTarget != nullptr || KillTarget != nullptr || bKillAllSurvivorsRequested;
}

bool CheatManager::NeedsLocalExploits() const
{
	if (!cfg)
		return false;
	return cfg->bNoGunCooldown || cfg->bInfiniteBullets || cfg->bAntiDetection ||
		cfg->bNoDecoyCooldown || cfg->bSetDecoyNum || cfg->bFovChanger ||
		(cfg->bMagnetEnabled && cfg->bMagnetActive) ||
		cfg->bForceCharacterVisibility || bChangeNameRequested || cfg->bGodmode || cfg->bSpeedhack ||
		cfg->bFly || cfg->bNoclip || cfg->bNoRecoil ||
		cfg->bAimbot || cfg->bTriggerbot || cfg->bSilentAim ||
		cfg->bOverrideNameplateStats;
}

bool CheatManager::NeedsGameThreadTick() const
{
	if (!cfg)
		return false;
	if (cfg->bInitHooks || NeedsLocalExploits())
		return true;
	if (cfg->bPreventKick)
		return true;
	if (TeleportTarget || KillTarget || bKillAllSurvivorsRequested || bReturnToLobbyRequested)
		return true;
	if (!killAllQueue.empty())
		return true;
	if (g_camo && g_camo->settings.hotkeysEnabled)
		return true;
	return false;
}

void CheatManager::SyncDecoyCooldownState(SDK::ABP_FirstPersonCharacter_Main_C* character)
{
	auto* leon = AsLeonCharacter(character);
	if (!leon || !cfg || !DecoyExploitsReady())
		return;
	if (!cfg->bNoDecoyCooldown && !cfg->bSetDecoyNum)
		return;
	// Same teardown guard as HandleSetDecoyNum: don't write into a character that is
	// being destroyed or when we're not in a live match (round-end teardown).
	if (!inMatchCached.load(std::memory_order_acquire) || SafeActorBeingDestroyed(character) ||
		!IsObjectUsable(character))
		return;
	// Intentionally NOT gated on decoyQuiesceUntilMs_: paintable quiesce is for
	// MaxDecoySpawnCount writes only. Pausing cooldown sync made clone icons go grey
	// whenever a decoy was destroyed (5s pause per drop).

	__try
	{
		leon->DecoyCoolTimeDefault = 0.0;

		int desiredSlots = leon->DecoyCoolTimes.Num();
		if (cfg->bSetDecoyNum)
		{
			desiredSlots = cfg->iDecoyCount;
			if (desiredSlots < 0)
				desiredSlots = 0;
			if (desiredSlots > 99)
				desiredSlots = 99;
		}
		else if (leon->RuntimePaintable && IsObjectUsable(leon->RuntimePaintable))
		{
			if (lastDecoyCountApplied > desiredSlots)
				desiredSlots = lastDecoyCountApplied;
		}

		while (leon->DecoyCoolTimes.Num() < desiredSlots)
		{
			if (!leon->DecoyCoolTimes.Add(1.0))
				break;
		}

		const int slotCount = leon->DecoyCoolTimes.Num();
		for (int j = 0; j < slotCount; ++j)
		{
			if (!leon->DecoyCoolTimes.IsValidIndex(j))
				continue;
			leon->DecoyCoolTimes[j] = 1.0;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		PhLog("[EXPLOITS:DECOY-NUM] SyncDecoyCooldownState fault — skipped frame\n");
	}
}

void CheatManager::TrackDecoyLifecycle(SDK::URuntimePaintableComponent* paintable)
{
	if (!paintable || !IsObjectValid(paintable))
		return;
	// SpawnedDecoyActors removed from the current RuntimePaintableComponent SDK layout.
	(void)paintable;
}

namespace
{
	struct MovementSetWalkSpeedParms
	{
		double WalkSpeed = 0.0;
	};

	struct MovementSetGravityParms
	{
		bool bOverrideGravity = false;
		uint8_t Pad[7]{};
		SDK::FVector GravityAcceleration{};
	};

	struct MovementAddForceMoverParms
	{
		SDK::FVector Velocity{};
		SDK::EMoverLaunchVelocityMode Mode = SDK::EMoverLaunchVelocityMode::Override;
	};

	struct MovementReplicateVelocityParms
	{
		SDK::FVector NewVel{};
	};

	struct MovementSetSpectatingStateParms
	{
		bool State = false;
	};

	// Read a capsule's current collision setting so disabling noclip can restore
	// the exact original value (the body capsule and the overlap capsule ship
	// with different settings; forcing both to QueryAndPhysics broke movement).
	bool SafeGetCapsuleCollision(SDK::UCapsuleComponent* capsule, SDK::ECollisionEnabled& out)
	{
		if (!capsule || !CheatManager::IsObjectValid(capsule))
			return false;
		__try
		{
			out = capsule->GetCollisionEnabled();
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void SafeSetCapsuleCollision(SDK::UCapsuleComponent* capsule, SDK::ECollisionEnabled mode)
	{
		if (!capsule || !CheatManager::IsObjectValid(capsule))
			return;
		__try
		{
			capsule->SetCollisionEnabled(mode);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}

	void CallSetWalkSpeed(SDK::ABP_FirstPersonCharacter_Main_C* mainChar, double walkSpeed)
	{
		if (!mainChar || !CheatManager::IsObjectValid(mainChar) || !mainChar->Class)
			return;
		SDK::UFunction* fn = mainChar->Class->GetFunction("BP_FirstPersonCharacter_Main_C", "SetWalkSpeed");
		if (!fn)
			return;
		MovementSetWalkSpeedParms parms{};
		parms.WalkSpeed = walkSpeed;
		InvokeNativeProcessEvent(mainChar, fn, &parms);
	}

	void CallSetGravity(SDK::ABP_FirstPersonCharacter_Main_C* mainChar, bool overrideGravity, const SDK::FVector& gravity)
	{
		if (!mainChar || !CheatManager::IsObjectValid(mainChar) || !mainChar->Class)
			return;
		SDK::UFunction* fn = mainChar->Class->GetFunction("BP_FirstPersonCharacter_Main_C", "SetGravity");
		if (!fn)
			return;
		MovementSetGravityParms parms{};
		parms.bOverrideGravity = overrideGravity;
		parms.GravityAcceleration = gravity;
		InvokeNativeProcessEvent(mainChar, fn, &parms);
	}

	void CallAddForceMover(SDK::ABP_FirstPersonCharacter_Main_C* mainChar, const SDK::FVector& velocity)
	{
		if (!mainChar || !CheatManager::IsObjectValid(mainChar) || !mainChar->Class)
			return;
		SDK::UFunction* fn = mainChar->Class->GetFunction("BP_FirstPersonCharacter_Main_C", "AddForce_Mover_");
		if (!fn)
			return;
		MovementAddForceMoverParms parms{};
		parms.Velocity = velocity;
		parms.Mode = SDK::EMoverLaunchVelocityMode::Override;
		InvokeNativeProcessEvent(mainChar, fn, &parms);
	}

	void CallReplicateVelocity(SDK::ABP_FirstPersonCharacter_Main_C* mainChar, const SDK::FVector& velocity)
	{
		if (!mainChar || !CheatManager::IsObjectValid(mainChar) || !mainChar->Class)
			return;
		SDK::UFunction* fn = mainChar->Class->GetFunction("BP_FirstPersonCharacter_Main_C", "ReplicateVelocity");
		if (!fn)
			return;
		MovementReplicateVelocityParms parms{};
		parms.NewVel = velocity;
		InvokeNativeProcessEvent(mainChar, fn, &parms);
	}

	void CallSetSpectatingState(SDK::ABP_FirstPersonCharacter_Main_C* mainChar, bool state)
	{
		if (!mainChar || !CheatManager::IsObjectValid(mainChar) || !mainChar->Class)
			return;
		SDK::UFunction* fn = mainChar->Class->GetFunction("BP_FirstPersonCharacter_Main_C", "SetSpectatingState");
		if (!fn)
			return;
		MovementSetSpectatingStateParms parms{};
		parms.State = state;
		InvokeNativeProcessEvent(mainChar, fn, &parms);
	}

	// True while it is safe to consume raw key state for game actions: the game
	// window has focus and the cheat menu is closed (so WASD typed into the menu
	// never moves the character).
	bool GameInputAllowed()
	{
		if (!cfg || cfg->bMenuOpen)
			return false;
		const HWND fg = GetForegroundWindow();
		return fg && Process::Hwnd && (fg == Process::Hwnd || IsChild(Process::Hwnd, fg));
	}

	bool SafeTeleportActor(SDK::AActor* actor, const SDK::FVector& dest, const SDK::FRotator& rot)
	{
		if (!actor || !CheatManager::IsObjectValid(actor))
			return false;
		__try
		{
			actor->K2_TeleportTo(dest, rot);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// Drive fly/noclip through the game's Mover velocity API instead of
	// K2_TeleportTo — teleport fought the Mover sim each tick and left noclip
	// feeling like movement was broken.
	void SafeAddForceMover(SDK::ABP_FirstPersonCharacter_Main_C* mainChar, const SDK::FVector& velocity)
	{
		if (!mainChar || !CheatManager::IsObjectValid(mainChar))
			return;
		__try
		{
			CallAddForceMover(mainChar, velocity);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}

	void SafeReplicateVelocity(SDK::ABP_FirstPersonCharacter_Main_C* mainChar, const SDK::FVector& velocity)
	{
		if (!mainChar || !CheatManager::IsObjectValid(mainChar))
			return;
		__try
		{
			CallReplicateVelocity(mainChar, velocity);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}

	// Spectator-style flight: WASD along camera axes, Space/C vertical. Uses
	// AddForce_Mover_(Override) so the Mover component owns the motion.
	void ApplyManualFlight(SDK::ABP_FirstPersonCharacter_Main_C* mainChar)
	{
		static ULONGLONG lastMs = 0;
		const ULONGLONG now = GetTickCount64();
		if (lastMs == 0)
			lastMs = now;
		const double dt = (double)(now - lastMs) / 1000.0;
		lastMs = now;
		if (dt <= 0.0 || dt > 1.0)
			return;

		if (!GameInputAllowed())
			return;

		double fwd = 0.0, right = 0.0, up = 0.0;
		if (GetAsyncKeyState('W') & 0x8000) fwd += 1.0;
		if (GetAsyncKeyState('S') & 0x8000) fwd -= 1.0;
		if (GetAsyncKeyState('D') & 0x8000) right += 1.0;
		if (GetAsyncKeyState('A') & 0x8000) right -= 1.0;
		if (GetAsyncKeyState(VK_SPACE) & 0x8000) up += 1.0;
		if ((GetAsyncKeyState('C') & 0x8000) || (GetAsyncKeyState(VK_LCONTROL) & 0x8000)) up -= 1.0;

		if (fwd == 0.0 && right == 0.0 && up == 0.0)
		{
			const SDK::FVector zeroVel{};
			SafeReplicateVelocity(mainChar, zeroVel);
			SafeAddForceMover(mainChar, zeroVel);
			return;
		}

		SDK::FRotator viewRot{};
		if (!SafeGetControlRotationFromPawn(mainChar, viewRot))
			return;

		constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
		const double yaw = viewRot.Yaw * kDegToRad;
		const double pitch = viewRot.Pitch * kDegToRad;
		const SDK::FVector forwardVec{ cos(pitch) * cos(yaw), cos(pitch) * sin(yaw), sin(pitch) };
		const SDK::FVector rightVec{ -sin(yaw), cos(yaw), 0.0 };

		SDK::FVector dir{
			forwardVec.X * fwd + rightVec.X * right,
			forwardVec.Y * fwd + rightVec.Y * right,
			forwardVec.Z * fwd + up,
		};
		const double len = sqrt(dir.X * dir.X + dir.Y * dir.Y + dir.Z * dir.Z);
		if (len < 0.0001)
			return;

		double speed = (cfg->fFlySpeed > 0.0f) ? (double)cfg->fFlySpeed : 1200.0;
		if (GetAsyncKeyState(VK_LSHIFT) & 0x8000)
			speed *= 2.0;
		speed /= len;

		const SDK::FVector vel{ dir.X * speed, dir.Y * speed, dir.Z * speed };
		SafeAddForceMover(mainChar, vel);
		SafeReplicateVelocity(mainChar, vel);
	}

	void ApplyMovementExploits(SDK::ABP_FirstPersonCharacter_Main_C* baseClass)
	{
		if (!baseClass || !CheatManager::IsObjectValid(baseClass) || !cfg)
			return;

		auto* mainChar = static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(baseClass);
		if (!mainChar || !CheatManager::IsObjectValid(mainChar))
			return;

		const bool wantsFly = cfg->bFly;
		const bool wantsNoclip = cfg->bNoclip;
		// Noclip disables capsule collision; with NoCollision the Mover can't do
		// normal ground locomotion, so we drive velocity the same way as fly.
		const bool wantsManualMove = wantsFly || wantsNoclip;
		const bool wantsSpeed = cfg->bSpeedhack && cfg->fSpeedMultiplier > 1.0f;
		static bool movementHacked = false;
		static bool noclipActive = false;
		static bool manualMoveActive = false;

		__try
		{
			if (cfg->bGodmode)
			{
				mainChar->Invincible = true;
				if (mainChar->MaxHealthValue > 0.0)
					mainChar->Health = mainChar->MaxHealthValue;
				// Clear any death state that slipped through before the RPC block
				// caught it, so a momentary hit can't leave us ragdolled/dead.
				mainChar->Dead = false;
				CallSetSpectatingState(mainChar, false);
			}

			if (wantsSpeed)
			{
				const double mult = static_cast<double>(cfg->fSpeedMultiplier);
				const double baseMultiply = mainChar->DefaultMoveMultiply > 0.0
					? mainChar->DefaultMoveMultiply
					: 1.0;
				mainChar->MoveSpeedMultiply = baseMultiply * mult;
				mainChar->GlobalSpeed = mult;

				const double baseWalk = mainChar->DefaultMaxWalkSpeed > 0.0
					? mainChar->DefaultMaxWalkSpeed
					: 600.0;
				CallSetWalkSpeed(mainChar, baseWalk * mult);
			}

			// Fly / noclip: zero gravity (capsule has no floor when noclip) and drive
			// velocity through the Mover API instead of teleporting the actor.
			if (wantsManualMove)
			{
				manualMoveActive = true;
				const SDK::FVector zeroGravity{ 0.0, 0.0, 0.0 };
				CallSetGravity(mainChar, true, zeroGravity);
				movementHacked = true;
				ApplyManualFlight(mainChar);
			}
			else if (manualMoveActive)
			{
				const SDK::FVector zeroVel{};
				SafeReplicateVelocity(mainChar, zeroVel);
				SafeAddForceMover(mainChar, zeroVel);
				manualMoveActive = false;
			}

			if (!wantsManualMove && movementHacked)
			{
				const SDK::FVector defaultGravity{ 0.0, 0.0, -980.0 };
				CallSetGravity(mainChar, false, defaultGravity);
				movementHacked = false;
			}

			// Noclip capsules: capture each capsule's real collision setting on
			// enable and restore exactly that on disable. The old code forced
			// both capsules to QueryAndPhysics on restore, which does not match
			// the game's defaults and contributed to the character staying
			// frozen after noclip was turned off.
			static SDK::ECollisionEnabled savedBodyCollision = SDK::ECollisionEnabled::QueryAndPhysics;
			static SDK::ECollisionEnabled savedOverlapCollision = SDK::ECollisionEnabled::QueryOnly;
			static bool collisionSaved = false;

			if (cfg->bNoclip)
			{
				if (!noclipActive)
				{
					collisionSaved =
						SafeGetCapsuleCollision(mainChar->BodyCapsule, savedBodyCollision) &&
						SafeGetCapsuleCollision(mainChar->OverapCollision, savedOverlapCollision);
					noclipActive = true;
				}
				SafeSetCapsuleCollision(mainChar->BodyCapsule, SDK::ECollisionEnabled::NoCollision);
				SafeSetCapsuleCollision(mainChar->OverapCollision, SDK::ECollisionEnabled::NoCollision);
			}
			else if (noclipActive)
			{
				SafeSetCapsuleCollision(mainChar->BodyCapsule,
					collisionSaved ? savedBodyCollision : SDK::ECollisionEnabled::QueryAndPhysics);
				SafeSetCapsuleCollision(mainChar->OverapCollision,
					collisionSaved ? savedOverlapCollision : SDK::ECollisionEnabled::QueryOnly);
				noclipActive = false;
			}

			// No recoil / no camera shake: zero the shake scalars the gun and
			// movement effects feed from, remembering the shipped values so the
			// toggle restores them.
			static double savedShakeValue = 0.0;
			static double savedShakeRotation = 0.0;
			static bool shakeSaved = false;
			if (cfg->bNoRecoil)
			{
				if (!shakeSaved)
				{
					savedShakeValue = mainChar->ShakeValue;
					savedShakeRotation = mainChar->ShakeRotationValue;
					shakeSaved = true;
				}
				mainChar->ShakeValue = 0.0;
				mainChar->ShakeRotationValue = 0.0;
				if (mainChar->BPC_CameraShake && CheatManager::IsObjectValid(mainChar->BPC_CameraShake))
					mainChar->BPC_CameraShake->ShakeEnd();
			}
			else if (shakeSaved)
			{
				mainChar->ShakeValue = savedShakeValue;
				mainChar->ShakeRotationValue = savedShakeRotation;
				shakeSaved = false;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}
}

// GAME THREAD: arm bullet-trace overrides for the next hunter shot window.
// hkProcessEvent patches LineTraceSingle/SphereTraceSingle OutHit + End, and
// SpawnShotEffect endpoint, so the kill goes through the game's own shot path.
void CheatManager::ArmCombatShotRedirect(SDK::AActor* target, const SDK::FVector& hitLocation)
{
	if (!target || !IsObjectValid(target))
		return;
	if (target->IsA(SDK::ABP_cLeonDecoy_Base_C::StaticClass()))
		return;

	g_combatRedirect.active = true;
	g_combatRedirect.target = target;
	g_combatRedirect.hitLocation = hitLocation;
	g_combatRedirect.expireMs = GetTickCount64() + 250;
}

void CheatManager::DisarmCombatShotRedirect()
{
	g_combatRedirect.active = false;
	g_combatRedirect.target = nullptr;
	g_combatRedirect.expireMs = 0;
}

void CheatManager::CacheSilentAimTarget(SDK::AActor* target, const SDK::FVector& hitLocation)
{
	if (!target || !IsObjectValid(target))
	{
		g_combatRedirect.silentReady = false;
		g_combatRedirect.silentTarget = nullptr;
		return;
	}
	g_combatRedirect.silentTarget = target;
	g_combatRedirect.silentHit = hitLocation;
	g_combatRedirect.silentReady = true;
}

void CheatManager::RequestHunterShot(SDK::ABP_FirstPersonCharacter_Main_C* hunter)
{
	if (!hunter || !IsObjectValid(hunter) || !hunter->Class)
		return;

	SDK::UFunction* fn = g_fnHunterInpActShot;
	if (!fn)
	{
		fn = hunter->Class->GetFunction(
			"BP_FirstPersonCharacter_Main_C", "InpActEvt_IA_Shot_K2Node_EnhancedInputActionEvent_18");
		if (fn)
			g_fnHunterInpActShot = fn;
	}
	if (!fn)
	{
		fn = hunter->Class->GetFunction(
			"BP_FirstPersonCharacter_cLeon_Character_Hunter_C", "InpActEvt_IA_Shot_K2Node_EnhancedInputActionEvent_3");
		if (fn)
			g_fnHunterInpActShot = fn;
	}
	if (!fn)
		return;

	const SDK::FInputActionValue actionValue = SDK::UEnhancedInputLibrary::MakeInputActionValueOfType(
		1.0, 0.0, 0.0, SDK::EInputActionValueType::Boolean);

	SDK::Params::BP_FirstPersonCharacter_cLeon_Character_Hunter_C_InpActEvt_IA_Shot_K2Node_EnhancedInputActionEvent_3 parms{};
	parms.ActionValue_InpActEvt_IA_Shot_K2Node_EnhancedInputActionEvent_3 = actionValue;
	parms.ElapsedTime_InpActEvt_IA_Shot_K2Node_EnhancedInputActionEvent_3 = 0.0f;
	parms.TriggeredTime_InpActEvt_IA_Shot_K2Node_EnhancedInputActionEvent_3 = 0.0f;
	parms.SourceAction_InpActEvt_IA_Shot_K2Node_EnhancedInputActionEvent_3 = nullptr;
	hunter->ProcessEvent(fn, &parms);
}

// GAME THREAD: Phase 4 combat. One pass picks the enemy nearest the crosshair
// (screen-space distance to the configured aim bone) and then aimbot,
// triggerbot and silent aim all act on that single pick.
void CheatManager::HandleCombat(FrameContext& ctx, SDK::TArray<SDK::AActor*>& Players)
{
	if (!cfg || (!cfg->bAimbot && !cfg->bTriggerbot && !cfg->bSilentAim))
		return;
	if (!inMatchCached.load(std::memory_order_acquire))
		return;
	if (!ctx.MyPlayer || !IsObjectValid(ctx.MyPlayer) ||
		!ctx.PlayerController || !IsObjectValid(ctx.PlayerController))
		return;

	// Sample fire edge for trigger pacing and silent-aim pre-arm.
	static bool prevFireDown = false;
	const bool inputAllowed = GameInputAllowed();
	const bool fireDown = inputAllowed && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	const bool fireEdge = fireDown && !prevFireDown;
	prevFireDown = fireDown;

	if (!inputAllowed)
		return;

	const bool aimHeld = cfg->bAimbot && BindHeld(cfg->iAimKey);
	const bool triggerActive = cfg->bTriggerbot &&
		(cfg->iTriggerKey == 0 || BindHeld(cfg->iTriggerKey));
	const bool silentActive = cfg->bSilentAim && inputAllowed && IsHunter(ctx.MyPlayer);
	if (!aimHeld && !triggerActive && !silentActive)
		return;

	const float centerX = ctx.screenX * 0.5f;
	const float centerY = ctx.screenY * 0.5f;
	const int aimBone = (cfg->iAimBone == 1) ? skeleton::spine2 : skeleton::Head;

	SDK::AActor* bestActor = nullptr;
	SDK::FVector bestWorld{};
	float bestDist = 1.0e30f;

	for (int i = 0; i < Players.Num(); i++)
	{
		if (!Players.IsValidIndex(i))
			continue;
		SDK::AActor* actor = Players[i];
		if (!actor || actor == ctx.MyPlayer || !IsObjectValid(actor) || SafeActorBeingDestroyed(actor))
			continue;
		if (!actor->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass()))
			continue;
		auto* baseClass = static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(actor);
		if (!baseClass || IsDead(actor))
			continue;
		if (!IsEnemy(ctx.MyPlayer, baseClass))
			continue;
		if (cfg->bAimVisibleOnly && !SafeLineOfSightTo(ctx.PlayerController, actor))
			continue;

		SDK::FVector world{};
		if (!SafeGetBoneWorldPos(baseClass->Mesh, aimBone, world) &&
			!SafeGetActorLocation(actor, world))
			continue;

		SDK::FVector2D screen{};
		if (!SafeProjectWorldLocationToScreen(ctx.PlayerController, world, screen) ||
			!IsScreenCoordValid(screen))
			continue;

		const float dx = static_cast<float>(screen.X) - centerX;
		const float dy = static_cast<float>(screen.Y) - centerY;
		const float dist = sqrtf(dx * dx + dy * dy);
		if (dist < bestDist)
		{
			bestDist = dist;
			bestActor = actor;
			bestWorld = world;
		}
	}

	if (cfg->bSilentAim)
	{
		if (!bestActor || bestDist > cfg->fAimFov)
			CacheSilentAimTarget(nullptr, {});
		else
			CacheSilentAimTarget(bestActor, bestWorld);

		// Pre-arm redirect on click so trace patches are live before IA_Shot runs.
		if (silentActive && fireEdge && g_combatRedirect.silentReady && g_combatRedirect.silentTarget)
			ArmCombatShotRedirect(g_combatRedirect.silentTarget, g_combatRedirect.silentHit);
	}

	if (!bestActor || bestDist > cfg->fAimFov)
		return;

	if (aimHeld)
	{
		SDK::FVector camLoc{};
		bool haveCam = SafeGetCameraLocation(ctx.PlayerController, camLoc);
		if (!haveCam)
			haveCam = SafeGetActorLocation(ctx.MyPlayer, camLoc);
		SDK::FRotator current{};
		if (haveCam && SafeGetControlRotationFromPawn(ctx.MyPlayer, current))
		{
			constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
			const double dx = bestWorld.X - camLoc.X;
			const double dy = bestWorld.Y - camLoc.Y;
			const double dz = bestWorld.Z - camLoc.Z;
			const double desiredYaw = atan2(dy, dx) * kRadToDeg;
			const double desiredPitch = atan2(dz, sqrt(dx * dx + dy * dy)) * kRadToDeg;

			// Shortest angular path, eased by the smoothing factor (1 = snap).
			auto wrapDelta = [](double a) {
				while (a > 180.0) a -= 360.0;
				while (a < -180.0) a += 360.0;
				return a;
			};
			const double smooth = (cfg->fAimSmooth < 1.0f) ? 1.0 : static_cast<double>(cfg->fAimSmooth);
			const double t = 1.0 / smooth;

			SDK::FRotator out{};
			out.Pitch = current.Pitch + wrapDelta(desiredPitch - current.Pitch) * t;
			out.Yaw = current.Yaw + wrapDelta(desiredYaw - current.Yaw) * t;
			out.Roll = 0.0;
			SafeSetControlRotation(ctx.PlayerController, out);
		}
	}

	// Triggerbot: arm trace redirect and fire through the hunter's IA_Shot path.
	if (triggerActive && IsHunter(ctx.MyPlayer))
	{
		constexpr float kTriggerWindowPx = 22.0f;
		static ULONGLONG nextTriggerMs = 0;
		const ULONGLONG now = GetTickCount64();
		if (bestDist <= kTriggerWindowPx && now >= nextTriggerMs)
		{
			ArmCombatShotRedirect(bestActor, bestWorld);
			auto* hunter = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C*>(ctx.MyPlayer);
			RequestHunterShot(hunter);
			nextTriggerMs = now + 400;
		}
	}

	// Silent aim: redirect is armed in ProcessEvent when IA_Shot fires (see CombatPre).
}

void CheatManager::HandleGodmodeRecovery(FrameContext& ctx)
{
	if (!cfg || !cfg->bGodmode)
		return;
	if (!lastGodmodeCharacter_ || !IsObjectValid(lastGodmodeCharacter_) ||
		!lastGodmodeCharacter_->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass()))
	{
		return;
	}
	if (!ctx.PlayerController || !IsObjectValid(ctx.PlayerController))
		return;

	auto* body = static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(lastGodmodeCharacter_);
	ApplyMovementExploits(body);

	SDK::APawn* currentPawn = ctx.MyPlayer;
	if (currentPawn == body)
		return;

	__try
	{
		ctx.PlayerController->Possess(body);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}

void CheatManager::ApplyLocalPlayerExploits(FrameContext& ctx, SDK::ABP_FirstPersonCharacter_Main_C* baseClass)
{
	if (!baseClass || !IsObjectValid(baseClass))
		return;

	if (IsHunter(baseClass))
	{
		auto* hunter = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C*>(baseClass);
		if (!hunter || !IsObjectValid(hunter))
			return;

		hunter->IsChater = false;
		hunter->CheatCheck = 0;

		if (cfg->bNoGunCooldown)
		{
			hunter->GunCoolTime = 0.0;
			hunter->Gun_Cool_TimeDefault = 0.0;
		}

		if (cfg->bInfiniteBullets)
			hunter->InfinityBullet = true;
	}
	else if (!IsHunter(baseClass))
	{
		if (IsSurvivor(baseClass))
		{
			auto* survivor = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_Survivor_C*>(baseClass);
			if (survivor && IsObjectValid(survivor) && cfg->bAntiDetection)
				survivor->OverlapCheckCapsules.Clear();
		}

		auto* leonBody = AsLeonCharacter(baseClass);
		if (leonBody && leonBody->RuntimePaintable && IsObjectValid(leonBody->RuntimePaintable))
			TrackDecoyLifecycle(leonBody->RuntimePaintable);

		if (cfg->bNoDecoyCooldown || cfg->bSetDecoyNum)
			SyncDecoyCooldownState(baseClass);

		if (cfg->bSetDecoyNum)
			HandleSetDecoyNum(baseClass);
		else if (lastDecoyCountApplied != -1)
		{
			lastDecoyCountApplied = -1;
			lastDecoyCountConfigured = -1;
		}
	}

	ApplyMovementExploits(baseClass);
	(void)ctx;
}

void CheatManager::HandleSetDecoyNum(SDK::ABP_FirstPersonCharacter_Main_C* character)
{
	if (!cfg->bSetDecoyNum || !character || !DecoyExploitsReady())
		return;
	// Never touch the paintable while the owning character is mid-teardown or we're
	// outside a live match. IsObjectValid only checks the GC index, so a being-destroyed
	// component still passes it — poking SetMaxDecoySpawnCount / ServerSet then faults
	// deep in net serialization and can crash the game a few frames later.
	if (!inMatchCached.load(std::memory_order_acquire) || SafeActorBeingDestroyed(character) ||
		!IsObjectUsable(character))
	{
		lastDecoyPaintable = nullptr;
		decoyPaintableReadyTickMs = 0;
		return;
	}
	if (GetTickCount64() < decoyQuiesceUntilMs_)
		return;

	if (g_camo)
	{
		const auto bridgeState = g_camo->BridgeState();
		if (g_camo->IsBusy() || bridgeState == CamoBridgeState::Loading)
			return;
	}

	auto* leon = AsLeonCharacter(character);
	if (!leon)
		return;

	SDK::URuntimePaintableComponent* paintable = leon->RuntimePaintable;
	if (!paintable || !IsObjectUsable(paintable))
	{
		lastDecoyPaintable = nullptr;
		decoyPaintableReadyTickMs = 0;
		return;
	}

	const ULONGLONG now = GetTickCount64();
	if (paintable != lastDecoyPaintable)
	{
		lastDecoyPaintable = paintable;
		decoyPaintableReadyTickMs = now + 1500;
		lastDecoyCountApplied = -1;
	}
	if (decoyPaintableReadyTickMs != 0 && now < decoyPaintableReadyTickMs)
		return;

	int target = cfg->iDecoyCount;
	if (target < 0)
		target = 0;
	if (target > 99)
		target = 99;

	if (target != lastDecoyCountConfigured)
	{
		lastDecoyCountConfigured = target;
		lastDecoyCountApplied = -1;
	}

	int current = (lastDecoyCountApplied >= 0) ? lastDecoyCountApplied : 0;

	// MaxDecoySpawnCount field removed from the current RuntimePaintableComponent layout;
	// rely on SetMaxDecoySpawnCount ProcessEvent and our cached apply state.

	// Game overwrote our value (replication / teardown). Do NOT fire ProcessEvent to
	// fight it — that is the crash path from the console logs (was 2 -> re-apply).
	// Quietly re-patch the field only, or bail if we're clearly in a reset window.
	const bool gameOverwrote = (lastDecoyCountApplied == target && current != target);
	if (gameOverwrote)
	{
		if (current < target)
		{
			// Count dropped toward the default — treat as a reset window, not a race to win.
			decoyQuiesceUntilMs_ = now + kDecoyQuiesceMs;
			PhLog("[EXPLOITS:DECOY-NUM] count reset by game (%d -> %d) — pausing writes\n",
				target, current);
			return;
		}
	}

	if (current == target && lastDecoyCountApplied == target)
		return;
	if (lastDecoyCountApplied == target && (now - lastDecoyRpcTickMs) < 1000)
		return;

	const int oldCount = current;
	const bool firstApply = (lastDecoyCountApplied != target);

	if (firstApply && !decoyProcessEventDisabled_ && !gameOverwrote)
	{
		if (!CallSetMaxDecoySpawnCountLocal(paintable, target))
		{
			decoyProcessEventDisabled_ = true;
			decoyQuiesceUntilMs_ = now + (kDecoyQuiesceMs * 6);
			PhLog("[EXPLOITS:DECOY-NUM] ProcessEvent disabled for this session\n");
			return;
		}
	}

	if (target != lastDecoyCountApplied)
		PhLog("[EXPLOITS:DECOY-NUM] MaxDecoySpawnCount -> %d (was %d)%s\n",
			target, oldCount, decoyProcessEventDisabled_ ? " [field]" : "");

	lastDecoyCountApplied = target;
	lastDecoyRpcTickMs = now;
}

void CheatManager::HandleReturnToMainLobby(SDK::APlayerController* playerController)
{
	if (!bReturnToLobbyRequested)
		return;
	bReturnToLobbyRequested = false;

	if (!playerController || !IsObjectValid(playerController))
	{
		PhLog("[LOBBY] Return failed — no PlayerController (join a lobby or match first)\n");
		return;
	}

	if (!g_fnClientReturnToMainMenuWithTextReason)
		g_fnClientReturnToMainMenuWithTextReason = SDK::APlayerController::StaticClass()->GetFunction(
			"PlayerController", "ClientReturnToMainMenuWithTextReason");

	if (!g_fnClientReturnToMainMenuWithTextReason)
	{
		PhLog("[LOBBY] ClientReturnToMainMenuWithTextReason not found\n");
		return;
	}

	g_allowSelfReturnToMenu.store(true, std::memory_order_release);
	SDK::Params::PlayerController_ClientReturnToMainMenuWithTextReason parms{};
	playerController->ProcessEvent(g_fnClientReturnToMainMenuWithTextReason, &parms);
	g_allowSelfReturnToMenu.store(false, std::memory_order_release);

	PhLog("[LOBBY] Returning to main menu...\n");
}
