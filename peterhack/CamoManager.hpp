#pragma once



#include "CamoSettings.hpp"
#include "direct_bridge_abi.hpp"

#include <atomic>

#include <cstdint>

#include <mutex>

#include <string>

#include <thread>



enum class CamoJobKind

{

	None,

	Paint,

	Preview,

	UnPreview,

	Stop,

};



enum class CamoBridgeState

{

	Unloaded,

	Loading,

	Ready,

	Error,

};



class CamoManager

{

public:

	CamoSettings settings{};

	// Snapshot of the last paint job's replication/pacing metadata, parsed from the
	// bridge JSON response. Ints use -1 for "unknown"; the two radius flags are
	// tri-state (-1 unknown, 0 false, 1 true). Read a copy via Diagnostics().
	struct CamoDiagnostics
	{
		bool valid = false;
		unsigned long long updatedMs = 0;
		bool lastJobOk = false;
		std::string lastKind;
		int configuredBatch = -1;
		int configuredPacingMs = -1;
		int batchRequested = -1;
		int batchResolved = -1;
		int packedBatchCap = -1;
		int localBatchLimit = -1;
		int radiusUsedFallback = -1;
		int radiusWithinWindow = -1;
		bool autoMaterialRequested = false;
		int materialAutoOk = -1;
		std::string materialSource;
		std::string materialFailure;
		double materialMetallic = -1.0;
		double materialRoughness = -1.0;
	};

	CamoDiagnostics Diagnostics() const;

	// Live paint progress parsed from the bridge's .progress.json sidecar, which
	// the bridge rewrites (atomically) every ~250ms while a job runs. progress is
	// 0..1 across the whole job, so it only reaches 1.0 once the server batch
	// stream finishes - i.e. when other players see the paint fully applied.
	struct CamoProgress
	{
		bool valid = false;
		double progress = 0.0; // 0..1
		double elapsedMs = 0.0;
		int step = -1;
		int totalSteps = -1;
		bool terminal = false; // stage is a done/failed/cancelled marker
		std::string stage;
		std::string message;
	};

	CamoProgress ReadProgress() const;

	~CamoManager();



	void TickHotkeys(bool inMatch, bool menuOpen);

	void ClearHotkeyEdges();

	void DrawMenu();

	void OnMenuOpened();

	void OnSessionReset();

	void OnMatchLeft();

	bool EnsureBridge();

	void RequestPaint(CamoJobKind kind);

	void CancelActiveJob();



	const char* StatusText() const;
	const char* LastErrorText() const;
	// One-line summary for the Misc status HUD (empty when nothing to report).
	std::string HudStatusLine() const;

	bool IsBusy() const { return busy_.load(); }

	CamoBridgeState BridgeState() const { return bridgeState_.load(); }



private:

	std::atomic<CamoBridgeState> bridgeState_{ CamoBridgeState::Unloaded };

	std::atomic<bool> busy_{ false };

	std::atomic<bool> bridgeLoadActive_{ false };

	std::atomic<CamoJobKind> pendingKind_{ CamoJobKind::None };

	std::atomic<int> bridgePort_{ 0 };

	std::atomic<bool> bridgeUsesHello_{ false };

	std::atomic<ULONGLONG> lastBridgePingOkMs_{ 0 };



	mutable std::mutex statusMutex_;

	mutable std::mutex diagMutex_;

	CamoDiagnostics diag_;

	mutable std::mutex bridgeIdentityMutex_;

	std::mutex bridgeOpMutex_;

	std::string status_ = "Camo idle";

	std::string lastError_;

	std::uint8_t bridgeToken_[32]{};

	std::uint8_t bridgeInstanceGuid_[16]{};

	std::thread worker_;

	std::thread bridgeLoader_;



	std::wstring ResolveBridgeDllPath() const;

	std::wstring ResolveBridgeRoot() const;

	// Path of the bridge's progress sidecar (<loaded-bridge-dll>.progress.json).
	std::wstring BridgeProgressPath() const;

	// Best-effort delete of a stale sidecar so a new job starts from 0%.
	void ClearProgressSidecar() const;

	HMODULE GetBridgeModuleHandle() const;

	bool BridgeModuleLoaded() const;

	bool PingBridge(int timeoutMs = 1500);

	bool TcpRequest(const std::string& requestJson, std::string& responseOut, int timeoutMs);

	std::string BuildPaintPayload(DWORD pid, CamoJobKind kind) const;

	static const char* RegionModeJson(int mode);

	bool RunJob(CamoJobKind kind);

	void UpdateDiagnostics(const std::string& response, CamoJobKind kind, bool ok);

	void DrawDiagnostics();

	void SetStatus(const std::string& text);

	void SetError(const std::string& text);

	void StartWorker(CamoJobKind kind);

	void StartBridgeLoadAsync();

	void ClearBridgeIdentity();

	void MarkBridgeReady(int port);

	void InvalidateBridgeConnection();

	bool EnsureBridgeListenerStarted();

	bool RequestBridgeShutdown(int timeoutMs);

	bool InvokeBridgeStartV1(BridgeStartBlockV1& block);

	std::string BuildHelloJson() const;

	bool TcpRequestOnPort(int port, bool useHello, const std::string& requestJson, std::string& responseOut, int timeoutMs);

};

