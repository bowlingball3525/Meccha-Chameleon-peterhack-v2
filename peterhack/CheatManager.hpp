#include <atomic>

class CheatManager
{
public:	struct PlayerInfo
	{
		std::string Name;
		SDK::FVector Location;
		SDK::AActor* Actor;
		bool IsSurvivor = false; // resolved on the game thread; lets the menu filter without touching a live UObject
		int Role = 0;            // 0 = unknown, 1 = hunter, 2 = survivor
		float DistanceMeters = 0.0f;
		bool IsVisible = false;
	};

	// A render-ready snapshot of one player's ESP overlay, fully projected to screen space on the
	// game thread. The render thread only ever reads these - it never dereferences a live UObject,
	// which is what lets the draw pass run without racing the game thread's actor list.
	struct EspEntry
	{
		bool hasBox = false;
		SDK::FVector2D boxMin{};
		SDK::FVector2D boxMax{};
		std::vector<std::pair<SDK::FVector2D, SDK::FVector2D>> skeletonLines; // projected bone segments
		std::string name;
		int role = 0; // 0 = none, 1 = hunter, 2 = survivor, 3 = decoy
		float distanceMeters = 0.0f;
		int ammo = -1; // hunter bullets remaining; -1 = unknown/not a hunter
		bool hasSnapline = false;
		SDK::FVector2D snaplineScreen{};
		bool isVisible = false;
	};

	// Everything the render thread needs for one frame, produced wholesale on the game thread.
	struct EspSnapshot
	{
		std::vector<EspEntry> entries;
		std::vector<PlayerInfo> players; // backs the menu's teleport list
		bool magnetActive = false;
		float screenX = 0.0f;
		float screenY = 0.0f;
	};

private:
	// Per-frame world/player context. Resolved once at the top of each scan (ResolveContext) into a
	// local, then passed into the helpers below. Holding this as a local rather than as members means
	// a scan owns every pointer it touches and cannot alias another scan's scratch state - see the
	// single-threaded-ownership note on Init().
	struct FrameContext
	{
		SDK::UWorld* World = nullptr;
		SDK::APlayerController* PlayerController = nullptr;
		SDK::APawn* MyPlayer = nullptr;
		SDK::UGameplayStatics* GStatics = nullptr;
		int screenX = 0;
		int screenY = 0;
	};

	// Resolve the world/player pointer chain into ctx. Returns false if any link is null.
	bool ResolveContext(FrameContext& ctx);
	// Per-player helpers. The actor being processed (actor and its BaseClass cast) is passed in
	// explicitly rather than stashed on the object, so nothing here depends on shared mutable state.
	std::string ResolvePlayerName(SDK::AActor* actor, SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass);
	void UpdateForcedVisibility(SDK::AActor* actor, SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass);
	bool IsDead(SDK::AActor* actor);
	bool IsSurvivor(SDK::AActor* actor);
	bool IsSurvivor(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass);
	bool IsHunter(SDK::AActor* actor);
	bool IsHunter(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass);
	bool IsEnemy(SDK::APawn* myPlayer, SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass);

	// Game-thread builders: project the given actor's world state into a render-ready EspEntry.
	void BuildSkeletonLines(SDK::APlayerController* pc, SDK::USkinnedMeshComponent* mesh, std::vector<std::pair<SDK::FVector2D, SDK::FVector2D>>& out);
	bool ComputeBoundingBox(SDK::APlayerController* pc, SDK::USkinnedMeshComponent* mesh, SDK::FVector2D& BoxMin, SDK::FVector2D& BoxMax);
	void BuildEspEntry(SDK::APlayerController* pc, SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass, EspEntry& entry, const std::string& PlayerName, SDK::FVector Location, SDK::FVector MyLocation, bool IsVisible, bool fullMeshDetail);
	void BuildDecoyEntry(SDK::APlayerController* pc, SDK::ABP_cLeonDecoy_Base_C* decoy, EspEntry& entry, SDK::FVector Location, SDK::FVector MyLocation, bool fullMeshDetail);
	// Render-thread draw of a single prebuilt entry (ImGui only, no SDK/UObject access).
	void DrawEntry(const EspEntry& entry);

	void KillSurvivor(SDK::APawn* myPlayer, SDK::AActor* actor);
	// GAME THREAD: Phase 4 combat — aimbot, triggerbot, silent aim. Runs once per
	// scan with the freshly gathered character list.
	void HandleCombat(FrameContext& ctx, SDK::TArray<SDK::AActor*>& Players);
	void HandleTeleport(SDK::APawn* myPlayer, const std::unordered_set<SDK::AActor*>& currentActors);
	void HandleMagnet(SDK::APawn* myPlayer, SDK::AActor* selfActor, const std::unordered_set<SDK::AActor*>& currentActors, const SDK::FVector& MyLocation, SDK::TArray<SDK::AActor*>& Players, EspSnapshot& snap);
	void HandleKillTarget(SDK::APawn* myPlayer, const std::unordered_set<SDK::AActor*>& currentActors);
	void HandleKillAllSurvivors(SDK::APawn* myPlayer, const std::unordered_set<SDK::AActor*>& currentActors, SDK::UGameplayStatics* gStatics, SDK::UWorld* world);
	void HandleChangeName(SDK::APawn* myPlayer);
	void HandleNameplateStats(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass);
	void ApplyLocalPlayerExploits(FrameContext& ctx, SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass);
	void SyncDecoyCooldownState(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* character);
	void HandleSetDecoyNum(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* character);
	void TrackDecoyLifecycle(SDK::URuntimePaintableComponent* paintable);
	void HandleReturnToMainLobby(SDK::APlayerController* playerController);
	void HandleGodmodeRecovery(FrameContext& ctx);
	void ResetSessionState();
	void ResetSpawnTransition();
	SDK::UWorld* lastWorld_ = nullptr;
	SDK::ULevel* lastPersistentLevel_ = nullptr;
	SDK::APawn* lastLocalPawn_ = nullptr;
	SDK::APawn* lastGodmodeCharacter_ = nullptr;
	bool wasInMatch_ = false;
	bool wasSpectating_ = false;
	ULONGLONG worldStableAfterMs_ = 0;
	int inMatchStableFrames_ = 0;
	int espScansCompleted_ = 0;
	static constexpr ULONGLONG kJoinGraceMs = 8000;
	static constexpr int kRequiredStableFrames = 90;
	static constexpr int kEspWarmupScans = 60;
	static constexpr ULONGLONG kDecoyExploitDelayMs = 6000;
	static constexpr ULONGLONG kDecoyQuiesceMs = 5000;
	bool IsEspFullyWarm() const { return espScansCompleted_ >= kEspWarmupScans; }
	bool DecoyExploitsReady() const { return decoyExploitsReadyAfterMs_ != 0 && GetTickCount64() >= decoyExploitsReadyAfterMs_; }
	SDK::AActor* TeleportTarget = nullptr; // resolved by actor pointer, not list index, since the snapshot is rebuilt every frame
	SDK::FVector TeleportFallbackLocation{};
	bool TeleportHasFallback = false;
	SDK::AActor* KillTarget = nullptr;		 // single-player kill request, resolved by actor pointer like TeleportTarget
	bool bKillAllSurvivorsRequested = false;
	bool bChangeNameRequested = false;							// drained on the game thread in HandleChangeName
	std::string pendingChangeName;									// the name to apply on the next scan when bChangeNameRequested is set
	std::unordered_set<SDK::AActor*> killAllQueue; // pending "kill all" targets, drained one at a time with pacing
	ULONGLONG killAllNextMs_ = 0;
	static constexpr ULONGLONG kKillAllIntervalMs = 750;

	bool bReturnToLobbyRequested = false;
	bool menuLookInputLocked = false;
	int lastDecoyCountApplied = -1;
	int lastDecoyCountConfigured = -1;
	ULONGLONG lastDecoyRpcTickMs = 0;
	SDK::URuntimePaintableComponent* lastDecoyPaintable = nullptr;
	ULONGLONG decoyPaintableReadyTickMs = 0;
	ULONGLONG decoyExploitsReadyAfterMs_ = 0;
	int lastSpawnedDecoyCount_ = -1;
	ULONGLONG decoyQuiesceUntilMs_ = 0;
	// After any decoy ProcessEvent fault, never call ProcessEvent again until session reset —
	// the SEH catch is not enough; net/RPC teardown can still crash the game a few frames later.
	bool decoyProcessEventDisabled_ = false;

	// pendingSnapshot is written by the game thread (Init) and read by the render thread (RenderEsp)
	// under this mutex. drawSnapshot is the render thread's private working copy so it can draw
	// without holding the lock for the whole frame.
	std::mutex snapshotMutex;
	EspSnapshot pendingSnapshot;
	EspSnapshot drawSnapshot;

public:
	std::vector<PlayerInfo> PlayerInfos;
	std::atomic<bool> inMatchCached{ false };
	void RequestTeleport(SDK::AActor* Actor, const SDK::FVector& fallbackLocation = {});
	void RequestKillSurvivor(SDK::AActor* Actor) { KillTarget = Actor; }
	// GAME THREAD: direct kill used by silent aim after a shot event.
	void ExecuteSilentAimKill(SDK::APawn* hunter, SDK::AActor* target);
	void RequestKillAllSurvivors() { bKillAllSurvivorsRequested = true; }
	void RequestReturnToMainLobby() { bReturnToLobbyRequested = true; }
	// Queue a name change for our own player (e.g. to impersonate another player's name). Applied on
	// the game thread in HandleChangeName; storing the string copy means the menu never has to hold a
	// live UObject.
	void RequestChangeName(const std::string& Name)
	{
		pendingChangeName = Name;
		bChangeNameRequested = true;
	}
	std::unordered_set<SDK::AActor*> forcedVisibleActors;
	std::unordered_set<SDK::AActor*> deadActors;										// actors seen ragdolling; latched so ESP stays off after the corpse stops simulating physics
	std::unordered_map<SDK::AActor*, std::string> playerNameCache; // last-known name per actor, so ESP survives PlayerState replication blips
	void Init();																										// GAME THREAD: scan the world and publish a fresh snapshot
	void RenderEsp();																								// RENDER THREAD: draw the latest published snapshot
	void ApplyMenuInputLock(bool menuOpen);																			// GAME THREAD: block camera look while the menu is open
	void DumpBones(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass);
	void DumpDeathFlags(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass);

	// True if Obj is still the live object at its GObjects slot. The scan mutates game state inline on
	// the game thread, but an SDK call earlier in the same scan can destroy/GC an actor - calling into
	// a freed UObject is exactly the null-pointer-deep-in-engine-code crash this guards against.
	static bool IsObjectValid(SDK::UObject* Obj);
	// GAME THREAD: combat trace redirect helpers (called from HandleCombat + hkProcessEvent).
	static void CacheSilentAimTarget(SDK::AActor* target, const SDK::FVector& hitLocation);
	static void ArmCombatShotRedirect(SDK::AActor* target, const SDK::FVector& hitLocation);
	static void RequestHunterShot(SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C* hunter);
	static bool IsLocalPlayerSpectating(SDK::APlayerController* playerController);
	static bool IsLocalPlayerInMatch(SDK::APlayerController* playerController);
	static bool IsLiveCharacterBody(SDK::AActor* body);
	static bool IsAliveFreecam(SDK::APlayerController* playerController, SDK::AActor* cachedBody);
	bool NeedsLocalExploits() const;
	bool NeedsEspDraw() const;
	bool NeedsActorScan() const;
	bool NeedsGameThreadTick() const;
};
