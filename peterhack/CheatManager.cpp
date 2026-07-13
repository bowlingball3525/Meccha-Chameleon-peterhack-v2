#include "includes.hpp"

#pragma warning(disable : 4244)

#include <cmath>

#include "SDK/PenguinHotel_classes.hpp"
#include "SDK/PenguinHotel_parameters.hpp"
#include "SDK/BP_SpectatePawn_cLeon_classes.hpp"
#include "SDK/Engine_parameters.hpp"
#include "SDK/UMG_classes.hpp"
#include "SDK/UMG_parameters.hpp"

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

	void CallSetMaxDecoySpawnCount(SDK::URuntimePaintableComponent* paintable, int target, bool includeServerRpc)
	{
		if (!paintable || !CheatManager::IsObjectValid(paintable))
			return;

		SDK::UFunction* fnSet = paintable->Class->GetFunction("RuntimePaintableComponent", "SetMaxDecoySpawnCount");
		SDK::UFunction* fnServerSet = paintable->Class->GetFunction("RuntimePaintableComponent", "ServerSetMaxDecoySpawnCount");

		__try
		{
			if (fnSet)
			{
				SDK::Params::RuntimePaintableComponent_SetMaxDecoySpawnCount parms{};
				parms.NewMaxDecoySpawnCount = target;
				InvokeNativeProcessEvent(paintable, fnSet, &parms);
			}

			if (includeServerRpc && fnServerSet)
			{
				SDK::Params::RuntimePaintableComponent_ServerSetMaxDecoySpawnCount parms{};
				parms.NewMaxDecoySpawnCount = target;
				InvokeNativeProcessEvent(paintable, fnServerSet, &parms);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			PhLog("[EXPLOITS:DECOY-NUM] ProcessEvent fault — skipped RPC\n");
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
	TeleportTarget = nullptr;
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
	pendingDecoyServerRpc_ = false;
	lastSpawnedDecoyCount_ = -1;
	decoyQuiesceUntilMs_ = 0;
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

	if (g_camo)
		g_camo->ClearHotkeyEdges();
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

	const bool inMatch = IsLocalPlayerInMatch(ctx.PlayerController);
	const bool spectating = IsLocalPlayerSpectating(ctx.PlayerController);
	SDK::ULevel* level = ctx.World ? ctx.World->PersistentLevel : nullptr;
	const bool worldChanged = ctx.World != lastWorld_ || level != lastPersistentLevel_;
	const bool pawnChanged = ctx.MyPlayer != lastLocalPawn_;
	const bool leftMatch = wasInMatch_ && !inMatch;
	const bool enteredMatch = !wasInMatch_ && inMatch;
	const bool spectateEnded = wasSpectating_ && !spectating;

	if (worldChanged || leftMatch)
	{
		ResetSessionState();
		ForceRefreshKickFunctionPointers();
	}
	else if (enteredMatch || pawnChanged || spectateEnded)
	{
		ResetSpawnTransition();
	}

	if (worldChanged || leftMatch || enteredMatch || pawnChanged || spectateEnded)
	{
		if (worldChanged)
		{
			lastWorld_ = ctx.World;
			lastPersistentLevel_ = level;
			PhLog("[peterhack] World changed — session reset (%ums grace)\n", static_cast<unsigned>(kJoinGraceMs));
		}
		else if (spectateEnded || enteredMatch)
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

		if (worldChanged || enteredMatch || pawnChanged || spectateEnded)
			worldStableAfterMs_ = GetTickCount64() + kJoinGraceMs;
	}

	lastLocalPawn_ = ctx.MyPlayer;
	wasSpectating_ = spectating;
	wasInMatch_ = inMatch;
	inMatchCached.store(inMatch, std::memory_order_release);

	const bool joinStable = GetTickCount64() >= worldStableAfterMs_;
	if (inMatch && !spectating && joinStable)
		++inMatchStableFrames_;
	else
		inMatchStableFrames_ = 0;

	const bool levelReady = SafeLevelReadyForActorScan(level);
	const bool matchReady = inMatch && !spectating && joinStable && levelReady &&
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
			ctx.MyPlayer->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass()))
		{
			auto* localChar = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(ctx.MyPlayer);
			if (localChar && IsObjectValid(localChar))
				ApplyLocalPlayerExploits(ctx, localChar);
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

	// get players
	SDK::TArray<SDK::AActor*> Players;
	if (!SafeGetAllActorsOfClass(ctx.GStatics, ctx.World, SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass(), &Players))
	{
		if (cfg->bInitHooks)
		{
			std::lock_guard<std::mutex> lock(snapshotMutex);
			pendingSnapshot = std::move(snap);
		}
		return;
	}

	// Decoys ride the same entries/draw path as players (role 3). They have no
	// team, so Enemy Only never hides them - the toggle is the only gate.
	if (cfg->bDecoys)
	{
		SDK::TArray<SDK::AActor*> Decoys;
		if (!SafeGetAllActorsOfClass(ctx.GStatics, ctx.World, SDK::ABP_cLeonDecoy_Base_C::StaticClass(), &Decoys))
		{
			if (cfg->bInitHooks)
			{
				std::lock_guard<std::mutex> lock(snapshotMutex);
				pendingSnapshot = std::move(snap);
			}
			return;
		}
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

	// Track which actors exist this frame so we can drop stale entries from the
	// latched dead set below - otherwise a destroyed corpse's pointer could later
	// be reused by a live actor and wrongly suppress its ESP.
	std::unordered_set<SDK::AActor*> currentActors;

	for (int i = 0; i < Players.Num(); i++)
	{
		if (!Players.IsValidIndex(i))
			continue;

		SDK::AActor* actor = Players[i];
		if (!actor || !IsObjectValid(actor) || SafeActorBeingDestroyed(actor))
			continue;
		if (!actor->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass()))
			continue;
		auto* baseClass = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(actor);
		if (!baseClass)
			continue;

		currentActors.insert(actor);

		// Skip dead/ragdolled corpses (see IsDead for why the obvious flags don't
		// work).
		if (IsDead(actor))
			continue;

		SDK::FVector Location{};
		if (!SafeGetActorLocation(actor, Location))
			continue;

		const std::string PlayerName = ResolvePlayerName(actor, baseClass);
		const bool IsVisible = espFullyWarm ? SafeLineOfSightTo(ctx.PlayerController, actor) : false;

		if (actor == ctx.MyPlayer)
			continue;

		snap.players.push_back({ PlayerName, Location, actor, IsSurvivor(baseClass) });

		if (espFullyWarm)
			UpdateForcedVisibility(actor, baseClass);

		if (cfg->bDumpBones)
		{
			DumpBones(baseClass);
			cfg->bDumpBones = false;
		}

		if (cfg->bEnemyOnly && !IsEnemy(ctx.MyPlayer, baseClass))
			continue;

		EspEntry entry;
		BuildEspEntry(ctx.PlayerController, baseClass, entry, PlayerName, Location, MyLocation, IsVisible, espFullyWarm);
		snap.entries.push_back(std::move(entry));
	}

	if (IsObjectValid(ctx.MyPlayer) &&
		ctx.MyPlayer->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass()))
	{
		auto* localChar = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(ctx.MyPlayer);
		if (localChar && IsObjectValid(localChar))
		{
			if (espFullyWarm)
				ApplyLocalPlayerExploits(ctx, localChar);
			if (espFullyWarm && cfg->bMagnetEnabled && IsHunter(localChar))
				HandleMagnet(ctx.MyPlayer, ctx.MyPlayer, currentActors, MyLocation, Players, snap);
		}
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
	HandleKillAllSurvivors(ctx.MyPlayer, currentActors);
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
	SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass)
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
	SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass)
{
	if (cfg->bForceCharacterVisibility && !baseClass->BodyVisibility)
	{
		if (IsObjectValid(baseClass))
		{
			SDK::UFunction* fn = baseClass->Class->GetFunction("BP_FirstPersonCharacter_cLeon_Character_C", "OnRep_BodyVisibility");
			if (fn)
			{
				__try
				{
					baseClass->BodyVisibility = true;
					baseClass->ProcessEvent(fn, nullptr);
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
				}
			}
		}
		forcedVisibleActors.insert(actor);
	}
	else if (!cfg->bForceCharacterVisibility && forcedVisibleActors.count(actor))
	{
		if (IsObjectValid(baseClass))
		{
			SDK::UFunction* fn = baseClass->Class->GetFunction("BP_FirstPersonCharacter_cLeon_Character_C", "OnRep_BodyVisibility");
			if (fn)
			{
				__try
				{
					baseClass->BodyVisibility = false;
					baseClass->ProcessEvent(fn, nullptr);
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
				}
			}
		}
		forcedVisibleActors.erase(actor);
	}
}

// True when the current actor (obj/BaseClass) should be treated as a dead
// corpse and skipped.
//
// We can't use the obvious signals: the raw `Dead` field isn't replicated
// (stays 0 on remote corpses), IsLive() returns true for dead bodies in
// infection, and BodyVisibility is reserved for the Force Character Visibility
// feature (which reveals stealthed/invisible survivors), so it can't double as
// a death flag.
//
// Instead detect death by ragdoll: a live character - survivor or hunter - is
// animation-driven, so its mesh isn't simulating physics; only a dead body
// ragdolls (confirmed by logging: live = 0, corpse = 1). That flag is transient
// though - in infection the game hides the corpse and resets the ragdoll,
// flipping it back to 0 while the player is still dead - so we latch it: once
// an actor has ragdolled it stays dead for as long as it exists. The latch set
// is pruned to live actors back in Init() to avoid stale-pointer reuse.
bool CheatManager::IsDead(SDK::AActor* actor)
{
	if (!actor)
		return false;
	auto* baseClass = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(actor);
	if (!baseClass || !baseClass->Mesh)
		return false;

	if (baseClass->Mesh && IsObjectValid(baseClass->Mesh) && SafeIsAnySimulatingPhysics(baseClass->Mesh))
		deadActors.insert(actor);
	return deadActors.count(actor) > 0;
}

bool CheatManager::IsSurvivor(SDK::AActor* actor)
{
	if (!actor)
		return false;
	auto* baseClass = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(actor);
	if (!baseClass)
		return false;
	return baseClass->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_Survivor_C::StaticClass());
}

bool CheatManager::IsSurvivor(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass)
{
	if (!baseClass)
		return false;
	return baseClass->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_Survivor_C::StaticClass());
}

bool CheatManager::IsHunter(SDK::AActor* actor)
{
	if (!actor)
		return false;
	auto* baseClass = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(actor);
	if (!baseClass)
		return false;
	return baseClass->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C::StaticClass());
}

bool CheatManager::IsHunter(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass)
{
	if (!baseClass)
		return false;
	return baseClass->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C::StaticClass());
}

// True when the given actor is on the opposing team (survivor vs. hunter) from
// us.
bool CheatManager::IsEnemy(SDK::APawn* myPlayer, SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass)
{
	if (!myPlayer->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass()))
		return false;
	auto* MyChar = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(myPlayer);
	return MyChar->IsHunter != baseClass->IsHunter;
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
	SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass,
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
		entry.ammo = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C*>(baseClass)->CurrentBullet;

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
	SDK::ABP_cLeonDecoy_Base_C* decoy,
	EspEntry& entry,
	SDK::FVector Location,
	SDK::FVector MyLocation,
	bool fullMeshDetail)
{
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
			OverlayDrawList()->AddLine(ImVec2(seg.first.X, seg.first.Y), ImVec2(seg.second.X, seg.second.Y), colEsp, 1.0f);
		}
	}

	if (entry.hasBox && IsScreenCoordValid(entry.boxMin) && IsScreenCoordValid(entry.boxMax))
	{
		if (cfg->bNames)
			OverlayDrawList()->AddText(ImVec2(entry.boxMin.X, entry.boxMin.Y - 15), colEsp, entry.name.c_str());

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
				OverlayDrawList()->AddText(ImVec2(entry.boxMin.X + nameWidth, entry.boxMin.Y - 15), colWhite, roleText);
			}
		}

		if (cfg->bBox)
			draw->DrawBox(entry.boxMin.X, entry.boxMin.Y, entry.boxMax.X - entry.boxMin.X, entry.boxMax.Y - entry.boxMin.Y, colEsp, 1.0f);

		if (cfg->bDistance)
		{
			char DistanceText[32];
			snprintf(DistanceText, sizeof(DistanceText), "%.0fm", entry.distanceMeters);

			// center the label just under the box
			const ImVec2 TextSize = ImGui::CalcTextSize(DistanceText);
			const float TextX = (entry.boxMin.X + entry.boxMax.X) * 0.5f - TextSize.x * 0.5f;
			OverlayDrawList()->AddText(ImVec2(TextX, entry.boxMax.Y + 2), colEsp, DistanceText);
		}

		// if (cfg->bHunterAmmo && entry.ammo >= 0)
		// {
		// 	char AmmoText[32];
		// 	snprintf(AmmoText, sizeof(AmmoText), "Ammo: %d", entry.ammo);

		// 	const ImVec2 TextSize = ImGui::CalcTextSize(AmmoText);
		// 	const float TextX = (entry.boxMin.X + entry.boxMax.X) * 0.5f -
		// TextSize.x * 0.5f; 	const float TextY = entry.boxMax.Y + 2 +
		// (cfg->bDistance ? ImGui::GetTextLineHeight() : 0.0f);
		// 	OverlayDrawList()->AddText(ImVec2(TextX, TextY), colWhite,
		// AmmoText);
		// }
	}

	if (cfg->bLines && entry.hasSnapline && IsScreenCoordValid(entry.snaplineScreen))
	{
		const auto& io = ImGui::GetIO();
		OverlayDrawList()->AddLine(ImVec2(static_cast<float>(io.DisplaySize.x / 2), static_cast<float>(io.DisplaySize.y)), ImVec2(entry.snaplineScreen.X, entry.snaplineScreen.Y), colLine, 0.7f);
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
	if (TeleportTarget && currentActors.count(TeleportTarget) && myPlayer)
	{
		SDK::AActor* target = TeleportTarget;
		if (IsObjectValid(myPlayer) && IsObjectValid(target))
		{
			SDK::FRotator CurrentRotation = myPlayer->K2_GetActorRotation();
			myPlayer->K2_TeleportTo(target->K2_GetActorLocation(), CurrentRotation);
		}
	}
	TeleportTarget = nullptr;
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

		SDK::ABP_FirstPersonCharacter_cLeon_Character_C* otherBaseClass = (SDK::ABP_FirstPersonCharacter_cLeon_Character_C*)otherActor;
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
	//parms.SourcePlayerState = hunter->MyPlayerState;
	parms.SourcePlayerState = hunter->LastMyPlayerState;
	hunter->ProcessEvent(fn, &parms);
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
	const std::unordered_set<SDK::AActor*>& currentActors)
{
	// A fresh request seeds the pending set with everyone alive this frame. We
	// then kill at most one per frame (below) instead of looping with Sleep() - a
	// blocking loop here would stall the game thread for tens of ms per survivor,
	// since this whole scan runs inside the engine's ProcessEvent.
	if (bKillAllSurvivorsRequested)
	{
		bKillAllSurvivorsRequested = false;
		killAllQueue = currentActors;
	}

	if (killAllQueue.empty())
		return;

	// Discard targets that no longer exist this frame, then kill the next one
	// that's still present. KillSurvivor itself no-ops non-survivors /
	// already-dead actors, so a stale pick is harmless.
	for (auto it = killAllQueue.begin(); it != killAllQueue.end();)
	{
		SDK::AActor* actor = *it;
		if (!currentActors.count(actor))
		{
			it = killAllQueue.erase(it);
			continue;
		}

		KillSurvivor(myPlayer, actor);
		killAllQueue.erase(it);
		break; // one per frame paces the kills without blocking the game thread
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

	auto* myChar = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(myPlayer);
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
	SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass)
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

		return pawn->IsA(SDK::ABP_SpectatePawn_cLeon_C::StaticClass()) ||
			pawn->IsA(SDK::ASpectatorPawn::StaticClass());
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

		return pawn->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass());
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
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
	return NeedsEspDraw() || cfg->bDecoys || cfg->bMagnetEnabled || cfg->bForceCharacterVisibility ||
		TeleportTarget != nullptr || KillTarget != nullptr || bKillAllSurvivorsRequested;
}

bool CheatManager::NeedsLocalExploits() const
{
	if (!cfg)
		return false;
	return cfg->bNoGunCooldown || cfg->bInfiniteBullets || cfg->bAntiDetection ||
		cfg->bNoDecoyCooldown || cfg->bSetDecoyNum || cfg->bFovChanger || cfg->bMagnetEnabled ||
		cfg->bForceCharacterVisibility || bChangeNameRequested;
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

void CheatManager::ApplyNoDecoyCooldown(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* character)
{
	if (!character || !cfg->bNoDecoyCooldown || !DecoyExploitsReady())
		return;
	if (GetTickCount64() < decoyQuiesceUntilMs_)
		return;

	__try
	{
		// Only refresh existing cooldown slots — never grow the TArray to match an
		// inflated MaxDecoySpawnCount, which fights the game's decoy teardown path.
		character->DecoyCoolTimeDefault = 0.0;
		const int slotCount = character->DecoyCoolTimes.Num();
		for (int j = 0; j < slotCount; ++j)
		{
			if (!character->DecoyCoolTimes.IsValidIndex(j))
				continue;
			character->DecoyCoolTimes[j] = 1.0;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		PhLog("[EXPLOITS:DECOY-NUM] NoDecoyCooldown fault — skipped frame\n");
	}
}

void CheatManager::TrackDecoyLifecycle(SDK::URuntimePaintableComponent* paintable)
{
	if (!paintable || !IsObjectValid(paintable))
		return;

	int spawnedCount = -1;
	__try
	{
		spawnedCount = paintable->SpawnedDecoyActors.Num();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return;
	}

	const ULONGLONG now = GetTickCount64();
	if (lastSpawnedDecoyCount_ > 0 && spawnedCount >= 0 && spawnedCount < lastSpawnedDecoyCount_)
	{
		decoyQuiesceUntilMs_ = now + kDecoyQuiesceMs;
		pendingDecoyServerRpc_ = false;
		PhLog("[EXPLOITS:DECOY] clones cleared — pausing decoy writes for %ums\n",
			static_cast<unsigned>(kDecoyQuiesceMs));
	}

	if (spawnedCount >= 0)
		lastSpawnedDecoyCount_ = spawnedCount;
}

void CheatManager::ApplyLocalPlayerExploits(FrameContext& ctx, SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass)
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

		if (baseClass->RuntimePaintable && IsObjectValid(baseClass->RuntimePaintable))
			TrackDecoyLifecycle(baseClass->RuntimePaintable);

		if (cfg->bNoDecoyCooldown)
			ApplyNoDecoyCooldown(baseClass);

		if (cfg->bSetDecoyNum)
			HandleSetDecoyNum(baseClass);
		else if (lastDecoyCountApplied != -1)
		{
			lastDecoyCountApplied = -1;
			lastDecoyCountConfigured = -1;
		}
	}

	(void)ctx;
}

void CheatManager::HandleSetDecoyNum(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* character)
{
	if (!cfg->bSetDecoyNum || !character || !DecoyExploitsReady())
		return;
	if (GetTickCount64() < decoyQuiesceUntilMs_)
		return;

	if (g_camo)
	{
		const auto bridgeState = g_camo->BridgeState();
		if (g_camo->IsBusy() || bridgeState == CamoBridgeState::Loading)
			return;
	}

	SDK::URuntimePaintableComponent* paintable = character->RuntimePaintable;
	if (!paintable || !IsObjectValid(paintable))
	{
		lastDecoyPaintable = nullptr;
		decoyPaintableReadyTickMs = 0;
		pendingDecoyServerRpc_ = false;
		return;
	}

	const ULONGLONG now = GetTickCount64();
	if (paintable != lastDecoyPaintable)
	{
		lastDecoyPaintable = paintable;
		decoyPaintableReadyTickMs = now + 1500;
		lastDecoyCountApplied = -1;
		pendingDecoyServerRpc_ = false;
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
		pendingDecoyServerRpc_ = false;
	}

	const int current = paintable->MaxDecoySpawnCount;
	if (current == target && lastDecoyCountApplied == target && !pendingDecoyServerRpc_ && (now - lastDecoyRpcTickMs) < 3000)
		return;
	if (lastDecoyCountApplied == target && !pendingDecoyServerRpc_ && (now - lastDecoyRpcTickMs) < 1000)
		return;

	const int oldCount = current;
	const bool sendServerRpc = pendingDecoyServerRpc_ && (now - lastDecoyRpcTickMs) >= 1500;

	if (current != target)
	{
		__try
		{
			paintable->MaxDecoySpawnCount = target;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			PhLog("[EXPLOITS:DECOY-NUM] field write fault — skipped\n");
			return;
		}
	}

	CallSetMaxDecoySpawnCount(paintable, target, sendServerRpc);

	if (target != lastDecoyCountApplied || sendServerRpc)
		PhLog("[EXPLOITS:DECOY-NUM] MaxDecoySpawnCount -> %d (was %d)%s\n",
			target, oldCount, sendServerRpc ? " [server]" : "");

	lastDecoyCountApplied = target;
	lastDecoyRpcTickMs = now;
	pendingDecoyServerRpc_ = !sendServerRpc;
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