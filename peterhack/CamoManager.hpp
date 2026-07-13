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

	HMODULE GetBridgeModuleHandle() const;

	bool BridgeModuleLoaded() const;

	bool PingBridge(int timeoutMs = 1500);

	bool TcpRequest(const std::string& requestJson, std::string& responseOut, int timeoutMs);

	std::string BuildPaintPayload(DWORD pid, CamoJobKind kind) const;

	static const char* RegionModeJson(int mode);

	bool RunJob(CamoJobKind kind);

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

