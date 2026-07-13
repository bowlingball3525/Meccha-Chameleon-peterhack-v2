#define WIN32_LEAN_AND_MEAN
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <Windows.h>

#include "CamoManager.hpp"
#include "Log.hpp"
#include "imgui/imgui.h"
#include <Psapi.h>
#include <cstdio>
#include <cstring>
#include <direct.h>
#include <iostream>
#include <mutex>
#include <random>

#pragma comment(lib, "Ws2_32.lib")

extern std::atomic<DWORD> g_GameThreadId;

namespace
{
	constexpr int kBridgePort = 47654;
	const char* kBridgeDllName = "meccha-xenos-bridge.dll";
	const char* kProcessName = "PenguinHotel-Win64-Shipping.exe";

	std::once_flag g_wsaOnce;
	bool g_wsaReady = false;

	bool EnsureWinsockInitialized()
	{
		std::call_once(g_wsaOnce, []() {
			WSADATA wsa{};
			g_wsaReady = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
		});
		return g_wsaReady;
	}

	bool IsGameFocused()
	{
		HWND fg = GetForegroundWindow();
		if (!fg)
			return false;
		DWORD pid = 0;
		GetWindowThreadProcessId(fg, &pid);
		return pid == GetCurrentProcessId();
	}

	bool IsBlockedCamoHotkey(int vk)
	{
		switch (vk)
		{
		case VK_LBUTTON:
		case VK_RBUTTON:
		case VK_MBUTTON:
		case VK_XBUTTON1:
		case VK_XBUTTON2:
		case VK_INSERT:
			return true;
		default:
			return false;
		}
	}

	SHORT s_hotkeyPrev[256]{};

	void PrimeHotkeyEdge(int vk)
	{
		if (IsBlockedCamoHotkey(vk))
			return;
		s_hotkeyPrev[vk] = GetAsyncKeyState(vk);
	}

	bool HotkeyPressed(int vk)
	{
		if (IsBlockedCamoHotkey(vk))
			return false;

		const SHORT state = GetAsyncKeyState(vk);
		const bool down = (state & 0x8000) != 0;
		const bool edge = down && !(s_hotkeyPrev[vk] & 0x8000);
		s_hotkeyPrev[vk] = state;
		return edge;
	}

	void ParseHexColor(const char* hex, int& r, int& g, int& b)
	{
		r = g = b = 255;
		if (!hex || hex[0] != '#')
			return;
		unsigned rr = 255, gg = 255, bb = 255;
		if (strlen(hex) >= 7)
			sscanf_s(hex + 1, "%2x%2x%2x", &rr, &gg, &bb);
		r = static_cast<int>(rr);
		g = static_cast<int>(gg);
		b = static_cast<int>(bb);
	}

	void FillColorToFloat3(const char* hex, float outRgb[3])
	{
		int r = 255, g = 255, b = 255;
		ParseHexColor(hex, r, g, b);
		outRgb[0] = r / 255.0f;
		outRgb[1] = g / 255.0f;
		outRgb[2] = b / 255.0f;
	}

	void FillColorFromFloat3(const float rgb[3], char* hexOut, size_t hexOutSize)
	{
		const int r = static_cast<int>(rgb[0] * 255.0f + 0.5f);
		const int g = static_cast<int>(rgb[1] * 255.0f + 0.5f);
		const int b = static_cast<int>(rgb[2] * 255.0f + 0.5f);
		snprintf(hexOut, hexOutSize, "#%02X%02X%02X",
			r < 0 ? 0 : (r > 255 ? 255 : r),
			g < 0 ? 0 : (g > 255 ? 255 : g),
			b < 0 ? 0 : (b > 255 ? 255 : b));
	}

	void FillRandomBytes(std::uint8_t* out, std::size_t count)
	{
		std::random_device rd;
		std::uniform_int_distribution<int> dist(1, 255);
		for (std::size_t i = 0; i < count; ++i)
			out[i] = static_cast<std::uint8_t>(dist(rd));
	}

	std::string BytesToHexLower(const std::uint8_t* bytes, std::size_t count)
	{
		static const char* kHex = "0123456789abcdef";
		std::string out;
		out.reserve(count * 2);
		for (std::size_t i = 0; i < count; ++i)
		{
			out.push_back(kHex[(bytes[i] >> 4) & 0xF]);
			out.push_back(kHex[bytes[i] & 0xF]);
		}
		return out;
	}

	bool AllZeroBytes(const std::uint8_t* bytes, std::size_t count)
	{
		std::uint8_t combined = 0;
		for (std::size_t i = 0; i < count; ++i)
			combined |= bytes[i];
		return combined == 0;
	}
}

const char* CamoManager::RegionModeJson(int mode)
{
	return CamoSettings::RegionModeName(mode);
}

CamoManager::~CamoManager()
{
	if (worker_.joinable())
	{
		std::string resp;
		TcpRequest("{\"type\":\"cancel_paint\"}", resp, 2000);
		worker_.join();
	}
	if (bridgeLoader_.joinable())
		bridgeLoader_.join();
}

void CamoManager::SetStatus(const std::string& text)
{
	std::lock_guard<std::mutex> lock(statusMutex_);
	status_ = text;
}

void CamoManager::SetError(const std::string& text)
{
	std::lock_guard<std::mutex> lock(statusMutex_);
	lastError_ = text;
	status_ = text;
}

const char* CamoManager::StatusText() const
{
	std::lock_guard<std::mutex> lock(statusMutex_);
	return status_.c_str();
}

const char* CamoManager::LastErrorText() const
{
	std::lock_guard<std::mutex> lock(statusMutex_);
	return lastError_.c_str();
}

namespace
{
	std::wstring DesktopDeployDir()
	{
		wchar_t userProfile[MAX_PATH]{};
		const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH);
		if (n > 0 && n < MAX_PATH)
			return std::wstring(userProfile) + L"\\Desktop\\peterhack";
		return L"C:\\peterhack";
	}
}

std::wstring CamoManager::ResolveBridgeRoot() const
{
	wchar_t modulePath[MAX_PATH]{};
	HMODULE self = nullptr;
	static int moduleAnchor = 0;
	GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&moduleAnchor),
		&self);
	if (!self)
		self = GetModuleHandleW(L"peterhack.dll");
	if (!self)
		self = GetModuleHandleW(nullptr);
	GetModuleFileNameW(self, modulePath, MAX_PATH);

	std::wstring path(modulePath);
	const size_t slash = path.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
		path.resize(slash);

	const auto probe = [&](const std::wstring& root) {
		const std::wstring dll = root + L"\\" + std::wstring(kBridgeDllName, kBridgeDllName + strlen(kBridgeDllName));
		return GetFileAttributesW(dll.c_str()) != INVALID_FILE_ATTRIBUTES;
	};

	std::wstring candidates[] = {
		path + L"\\bridge",
		path,
		path + L"\\..\\bridge",
		DesktopDeployDir() + L"\\bridge",
		DesktopDeployDir(),
		L"C:\\peterhack\\bridge",
	};
	for (const auto& candidate : candidates)
	{
		wchar_t full[MAX_PATH]{};
		if (GetFullPathNameW(candidate.c_str(), MAX_PATH, full, nullptr) && probe(full))
			return full;
	}
	return DesktopDeployDir() + L"\\bridge";
}

std::wstring CamoManager::ResolveBridgeDllPath() const
{
	return ResolveBridgeRoot() + L"\\" + std::wstring(kBridgeDllName, kBridgeDllName + strlen(kBridgeDllName));
}

HMODULE CamoManager::GetBridgeModuleHandle() const
{
	HMODULE mod = GetModuleHandleW(L"meccha-xenos-bridge.dll");
	if (mod)
		return mod;
	return GetModuleHandleW(L"runtime-bridge.dll");
}

bool CamoManager::BridgeModuleLoaded() const
{
	return GetBridgeModuleHandle() != nullptr;
}

void CamoManager::ClearBridgeIdentity()
{
	std::lock_guard<std::mutex> lock(bridgeIdentityMutex_);
	std::memset(bridgeToken_, 0, sizeof(bridgeToken_));
	std::memset(bridgeInstanceGuid_, 0, sizeof(bridgeInstanceGuid_));
	bridgePort_.store(0);
	bridgeUsesHello_.store(false);
}

void CamoManager::MarkBridgeReady(int port)
{
	bridgePort_.store(port);
	lastBridgePingOkMs_.store(GetTickCount64());
	bridgeState_.store(CamoBridgeState::Ready);
}

void CamoManager::InvalidateBridgeConnection()
{
	bridgeState_.store(CamoBridgeState::Unloaded);
	lastBridgePingOkMs_.store(0);
}

std::string CamoManager::BuildHelloJson() const
{
	std::lock_guard<std::mutex> lock(bridgeIdentityMutex_);
	char buf[256];
	snprintf(buf, sizeof(buf),
		"{\"type\":\"hello\",\"bootstrap_protocol\":%u,\"instance_id\":\"%s\",\"token\":\"%s\"}",
		BridgeBootstrapProtocolV1,
		BytesToHexLower(bridgeInstanceGuid_, sizeof(bridgeInstanceGuid_)).c_str(),
		BytesToHexLower(bridgeToken_, sizeof(bridgeToken_)).c_str());
	return std::string(buf);
}

bool CamoManager::TcpRequestOnPort(int port, bool useHello, const std::string& requestJson, std::string& responseOut, int timeoutMs)
{
	responseOut.clear();
	if (port <= 0 || port > 65535)
		return false;

	if (!EnsureWinsockInitialized())
		return false;

	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
		return false;

	DWORD connectTimeout = static_cast<DWORD>(timeoutMs > 1500 ? 1500 : timeoutMs);
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&connectTimeout), sizeof(connectTimeout));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&connectTimeout), sizeof(connectTimeout));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(static_cast<u_short>(port));
	inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

	if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
	{
		closesocket(sock);
		return false;
	}

	DWORD ioTimeout = static_cast<DWORD>(timeoutMs);
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ioTimeout), sizeof(ioTimeout));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ioTimeout), sizeof(ioTimeout));

	auto sendLine = [&](const std::string& line) -> bool {
		std::string payload = line;
		if (payload.empty() || payload.back() != '\n')
			payload.push_back('\n');
		return send(sock, payload.c_str(), static_cast<int>(payload.size()), 0) != SOCKET_ERROR;
	};

	auto recvLine = [&](std::string& lineOut) -> bool {
		lineOut.clear();
		std::string raw;
		char buffer[65536];
		for (;;)
		{
			const int received = recv(sock, buffer, sizeof(buffer), 0);
			if (received <= 0)
				break;
			raw.append(buffer, buffer + received);
			const size_t lineEnd = raw.find('\n');
			if (lineEnd != std::string::npos)
			{
				lineOut = raw.substr(0, lineEnd);
				return !lineOut.empty();
			}
		}
		return false;
	};

	if (useHello)
	{
		std::string helloResponse;
		if (!sendLine(BuildHelloJson()) || !recvLine(helloResponse))
		{
			closesocket(sock);
			return false;
		}
		if (helloResponse.find("\"success\":true") == std::string::npos ||
			helloResponse.find("\"stage\":\"hello\"") == std::string::npos)
		{
			closesocket(sock);
			return false;
		}
	}

	if (!sendLine(requestJson))
	{
		closesocket(sock);
		return false;
	}

	if (!recvLine(responseOut))
	{
		closesocket(sock);
		return false;
	}

	closesocket(sock);
	return true;
}

bool CamoManager::TcpRequest(const std::string& requestJson, std::string& responseOut, int timeoutMs)
{
	int port = bridgePort_.load();
	if (port <= 0)
		port = kBridgePort;
	return TcpRequestOnPort(port, bridgeUsesHello_.load(), requestJson, responseOut, timeoutMs);
}

bool CamoManager::PingBridge(int timeoutMs)
{
	std::string response;
	if (!TcpRequest("{\"type\":\"ping\"}", response, timeoutMs))
		return false;
	const bool ok = response.find("\"success\":true") != std::string::npos ||
		response.find("\"message\":\"pong\"") != std::string::npos;
	if (ok)
		lastBridgePingOkMs_.store(GetTickCount64());
	return ok;
}

bool CamoManager::InvokeBridgeStartV1(BridgeStartBlockV1& block)
{
	HMODULE mod = GetBridgeModuleHandle();
	if (!mod)
		return false;

	const auto startFn = reinterpret_cast<BridgeStartV1Fn>(GetProcAddress(mod, "BridgeStartV1"));
	if (!startFn)
		return false;

	const DWORD exitCode = startFn(&block);
	if (exitCode == ERROR_SUCCESS &&
		block.result_state == BRIDGE_START_LISTENING &&
		block.bound_port > 0)
	{
		return true;
	}

	if (exitCode == ERROR_ALREADY_EXISTS &&
		block.result_state == BRIDGE_START_ALREADY_STARTED &&
		block.bound_port > 0)
	{
		return true;
	}

	PhLog("[CAMO] BridgeStartV1 failed exit=%lu state=%u win32=%lu winsock=%lu\n",
		static_cast<unsigned long>(exitCode),
		static_cast<unsigned>(block.result_state),
		static_cast<unsigned long>(block.win32_error),
		static_cast<unsigned long>(block.winsock_error));
	return false;
}

bool CamoManager::RequestBridgeShutdown(int timeoutMs)
{
	if (!bridgeUsesHello_.load() || bridgePort_.load() <= 0)
		return false;

	std::string response;
	if (!TcpRequest("{\"type\":\"shutdown\"}", response, timeoutMs))
		return false;

	const int waitAttempts = timeoutMs > 0 ? (timeoutMs / 125) + 1 : 40;
	for (int attempt = 0; attempt < waitAttempts; ++attempt)
	{
		if (!PingBridge(200))
			return true;
		Sleep(125);
	}
	return false;
}

bool CamoManager::EnsureBridgeListenerStarted()
{
	HMODULE mod = GetBridgeModuleHandle();
	if (!mod)
		return false;

	const auto startFn = reinterpret_cast<BridgeStartV1Fn>(GetProcAddress(mod, "BridgeStartV1"));
	if (!startFn)
	{
		bridgeUsesHello_.store(false);
		bridgePort_.store(kBridgePort);
		PhLog("[CAMO] Using legacy bridge listener on 127.0.0.1:%d\n", kBridgePort);
		return true;
	}

	bridgeUsesHello_.store(true);

	if (PingBridge(600))
	{
		if (bridgePort_.load() <= 0)
			bridgePort_.store(kBridgePort);
		return true;
	}

	if (bridgePort_.load() > 0 && !PingBridge(400))
	{
		PhLog("[CAMO] Bridge ping failed — requesting shutdown before restart\n");
		if (!RequestBridgeShutdown(12000))
			PhLog("[CAMO] Bridge shutdown timed out — retrying start\n");
		ClearBridgeIdentity();
		if (!BridgeModuleLoaded())
		{
			const std::wstring dllPath = ResolveBridgeDllPath();
			if (GetFileAttributesW(dllPath.c_str()) != INVALID_FILE_ATTRIBUTES)
				LoadLibraryW(dllPath.c_str());
		}
		for (int attempt = 0; attempt < 80 && PingBridge(200); ++attempt)
			Sleep(125);
	}

	BridgeStartBlockV1 block{};
	block.magic = BridgeStartMagicV1;
	block.size = sizeof(block);
	block.abi = BridgeStartAbiV1;
	block.pid = GetCurrentProcessId();
	block.protocol = BridgeBootstrapProtocolV1;
	block.requested_port = 0;
	block.result_state = BRIDGE_START_UNINITIALIZED;
	FillRandomBytes(block.instance_guid, sizeof(block.instance_guid));
	FillRandomBytes(block.token, sizeof(block.token));
	if (AllZeroBytes(block.token, sizeof(block.token)))
		block.token[0] = 1;

	if (!InvokeBridgeStartV1(block) || block.bound_port == 0)
	{
		if (block.result_state == BRIDGE_START_ALREADY_STARTED && block.bound_port > 0)
		{
			std::string pingResponse;
			if (TcpRequestOnPort(static_cast<int>(block.bound_port), false, "{\"type\":\"ping\"}", pingResponse, 800) &&
				pingResponse.find("\"success\":true") != std::string::npos)
			{
				bridgeUsesHello_.store(false);
				bridgePort_.store(static_cast<int>(block.bound_port));
				PhLog("[CAMO] Reconnected to existing legacy bridge on port %u\n", block.bound_port);
				return true;
			}
		}
		SetError("BridgeStartV1 failed to bind TCP listener");
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(bridgeIdentityMutex_);
		std::memcpy(bridgeToken_, block.token, sizeof(bridgeToken_));
		std::memcpy(bridgeInstanceGuid_, block.instance_guid, sizeof(bridgeInstanceGuid_));
	}
	bridgePort_.store(static_cast<int>(block.bound_port));
	PhLog("[CAMO] BridgeStartV1 listening on 127.0.0.1:%u\n", block.bound_port);
	return true;
}

bool CamoManager::EnsureBridge()
{
	std::lock_guard<std::mutex> bridgeLock(bridgeOpMutex_);

	const bool onGameThread = g_GameThreadId.load() != 0 && GetCurrentThreadId() == g_GameThreadId.load();
	const ULONGLONG now = GetTickCount64();

	if (bridgeState_.load() == CamoBridgeState::Ready)
	{
		if (onGameThread)
		{
			if (now - lastBridgePingOkMs_.load() < 3000)
				return true;
			if (PingBridge(250))
				return true;
			return false;
		}

		if (PingBridge(800))
			return true;

		PhLog("[CAMO] Bridge marked ready but TCP ping failed — restarting listener\n");
		InvalidateBridgeConnection();
	}

	if (onGameThread)
	{
		if (!BridgeModuleLoaded())
			return false;

		if (PingBridge(300))
		{
			if (bridgeState_.load() != CamoBridgeState::Ready)
				MarkBridgeReady(bridgePort_.load() > 0 ? bridgePort_.load() : kBridgePort);
			return true;
		}
		return false;
	}

	bridgeState_.store(CamoBridgeState::Loading);
	SetStatus("Loading camouflage bridge...");

	if (!BridgeModuleLoaded())
	{
		const std::wstring root = ResolveBridgeRoot();
		_wmkdir((DesktopDeployDir() + L"\\bridge").c_str());
		_wmkdir(root.c_str());

		const std::wstring dllPath = ResolveBridgeDllPath();
		if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			bridgeState_.store(CamoBridgeState::Error);
			SetError("Bridge DLL missing — build with build.bat or copy meccha-xenos-bridge.dll next to peterhack.dll");
			PhLog("[CAMO] Bridge DLL missing at expected path\n");
			return false;
		}

		if (!LoadLibraryW(dllPath.c_str()))
		{
			bridgeState_.store(CamoBridgeState::Error);
			SetError("LoadLibrary failed for meccha-xenos-bridge.dll");
			PhLog("[CAMO] LoadLibrary failed for meccha-xenos-bridge.dll\n");
			return false;
		}
		PhLog("[CAMO] Loaded meccha-xenos-bridge.dll\n");
	}

	if (!EnsureBridgeListenerStarted())
	{
		bridgeState_.store(CamoBridgeState::Error);
		return false;
	}

	for (int attempt = 0; attempt < 80; ++attempt)
	{
		if (PingBridge(1000))
		{
			std::string caps;
			if (TcpRequest("{\"type\":\"capabilities\"}", caps, 4000))
			{
				if (caps.find("mesh_first_paint") == std::string::npos)
				{
					bridgeState_.store(CamoBridgeState::Error);
					SetError("Bridge is not the mesh_first_paint build");
					PhLog("[CAMO] Bridge missing mesh_first_paint capability\n");
					return false;
				}
				PhLog("[CAMO] Bridge ready on 127.0.0.1:%d\n", bridgePort_.load());
			}
			MarkBridgeReady(bridgePort_.load());
			SetStatus("Camo bridge ready");
			return true;
		}
		Sleep(125);
	}

	bridgeState_.store(CamoBridgeState::Error);
	SetError("Bridge TCP not responding after start");
	PhLog("[CAMO] Bridge TCP not responding on 127.0.0.1:%d\n", bridgePort_.load());
	return false;
}

void CamoManager::StartBridgeLoadAsync()
{
	bool expected = false;
	if (!bridgeLoadActive_.compare_exchange_strong(expected, true))
		return;

	if (bridgeLoader_.joinable())
		bridgeLoader_.join();

	const auto state = bridgeState_.load();
	if (state != CamoBridgeState::Ready && state != CamoBridgeState::Loading)
	{
		bridgeState_.store(CamoBridgeState::Loading);
		SetStatus("Loading camouflage bridge...");
	}

	bridgeLoader_ = std::thread([this]() {
		EnsureBridge();
		bridgeLoadActive_.store(false);
	});
}

void CamoManager::OnMenuOpened()
{
	if (bridgeLoadActive_.load())
		return;
	StartBridgeLoadAsync();
}

std::string CamoManager::BuildPaintPayload(DWORD pid, CamoJobKind kind) const
{
	int r = 255, g = 255, b = 255;
	ParseHexColor(settings.fillColorHex, r, g, b);
	const double fr = r / 255.0;
	const double fg = g / 255.0;
	const double fb = b / 255.0;

	const bool preview = kind == CamoJobKind::Preview;
	const bool unpreview = kind == CamoJobKind::UnPreview;

	char buf[4096];
	snprintf(buf, sizeof(buf),
		"{\"type\":\"paint_full_route\","
		"\"native_apply_mode\":\"mesh_first_paint\","
		"\"route\":\"f10_mesh_first_paint\","
		"\"server_batch_rpc\":\"packed\","
		"\"packed_route\":\"component\","
		"\"preview_only\":%s,"
		"\"unpreview_only\":%s,"
		"\"research_artifacts\":false,"
		"\"process\":{\"pid\":%lu,\"name\":\"%s\"},"
		"\"tuning\":{"
		"\"brush_1_size_texels\":%.3f,"
		"\"brush_2_size_texels\":%.3f,"
		"\"brush_pipeline_version\":2,"
		"\"stroke_size_texels\":%.3f,"
		"\"server_batch_limit\":%d,"
		"\"server_batch_pacing_ms\":%d,"
		"\"coverage_step_texels\":%.3f,"
		"\"side_source_max_uv\":%.4f,"
		"\"front_back_source_max_uv\":%.4f,"
		"\"auto_material\":%s,"
		"\"metallic\":%.3f,"
		"\"roughness\":%.3f,"
		"\"front_region_mode\":\"%s\","
		"\"side_region_mode\":\"%s\","
		"\"back_region_mode\":\"%s\","
		"\"fill_color\":\"%s\","
		"\"fill_color_r\":%.8f,"
		"\"fill_color_g\":%.8f,"
		"\"fill_color_b\":%.8f,"
		"\"fill_metallic\":%.3f,"
		"\"fill_roughness\":%.3f"
		"}}",
		preview ? "true" : "false",
		unpreview ? "true" : "false",
		static_cast<unsigned long>(pid),
		kProcessName,
		settings.brush1Texels,
		settings.brush2Texels,
		settings.brush2Texels,
		settings.batchLimit,
		settings.batchPacingMs,
		settings.brush2Texels,
		settings.sideSourceMaxUv,
		settings.frontBackSourceMaxUv,
		settings.autoMaterial ? "true" : "false",
		settings.metallic,
		settings.roughness,
		RegionModeJson(settings.frontRegionMode),
		RegionModeJson(settings.sideRegionMode),
		RegionModeJson(settings.backRegionMode),
		settings.fillColorHex,
		fr, fg, fb,
		settings.fillMetallic,
		settings.fillRoughness);
	return std::string(buf);
}

bool CamoManager::RunJob(CamoJobKind kind)
{
	if (kind == CamoJobKind::Stop)
	{
		std::string resp;
		TcpRequest("{\"type\":\"cancel_paint\"}", resp, 5000);
		SetStatus("Paint stop requested");
		return true;
	}

	if (!EnsureBridge())
		return false;

	const DWORD pid = GetCurrentProcessId();
	const std::string payload = BuildPaintPayload(pid, kind);
	std::string response;
	const int timeoutMs = (kind == CamoJobKind::UnPreview || kind == CamoJobKind::Preview) ? 120000 : 600000;
	if (!TcpRequest(payload, response, timeoutMs))
	{
		SetError("Bridge paint request failed (TCP timeout)");
		return false;
	}

	if (response.find("\"success\":true") == std::string::npos)
	{
		SetError("Paint failed — see console");
		PhLog("[CAMO] response: %s\n", response.c_str());
		return false;
	}

	switch (kind)
	{
	case CamoJobKind::Preview: SetStatus("Preview applied"); break;
	case CamoJobKind::UnPreview: SetStatus("Preview cleared"); break;
	default: SetStatus("Camouflage applied"); break;
	}
	return true;
}

void CamoManager::StartWorker(CamoJobKind kind)
{
	if (busy_.load())
		return;
	if (worker_.joinable())
		worker_.join();

	busy_.store(true);
	pendingKind_.store(kind);
	worker_ = std::thread([this, kind]() {
		RunJob(kind);
		busy_.store(false);
		pendingKind_.store(CamoJobKind::None);
	});
}

void CamoManager::RequestPaint(CamoJobKind kind)
{
	if (kind == CamoJobKind::None)
		return;
	StartWorker(kind);
}

void CamoManager::CancelActiveJob()
{
	if (!busy_.load())
		return;

	// Do not go through StartWorker — it refuses work while busy, which made Stop a no-op.
	SetStatus("Stopping camouflage...");
	std::thread([this]() {
		std::string resp;
		if (TcpRequest("{\"type\":\"cancel_paint\"}", resp, 5000))
			SetStatus("Paint stop requested");
		else
			SetError("Stop request failed — bridge not responding");
	}).detach();
}

void CamoManager::OnSessionReset()
{
	CancelActiveJob();
	ClearHotkeyEdges();
}

void CamoManager::OnMatchLeft()
{
	OnSessionReset();
}

void CamoManager::ClearHotkeyEdges()
{
	const int keys[] = {
		settings.startHotkey,
		settings.previewHotkey,
		settings.unpreviewHotkey,
		settings.stopHotkey,
	};
	for (int vk : keys)
		PrimeHotkeyEdge(vk);
}

void CamoManager::TickHotkeys(bool inMatch, bool menuOpen)
{
	if (!IsGameFocused() || menuOpen)
		return;

	static bool s_wasInMatch = false;
	static ULONGLONG s_hotkeyArmTickMs = 0;
	const ULONGLONG now = GetTickCount64();
	if (inMatch && !s_wasInMatch)
	{
		ClearHotkeyEdges();
		s_hotkeyArmTickMs = now + 3000;
		PhLog("[CAMO] Match entered — camo hotkeys armed in 3s (enable in Camo tab if needed)\n");
		if (settings.hotkeysEnabled && bridgeState_.load() != CamoBridgeState::Ready)
			StartBridgeLoadAsync();
	}
	if (!inMatch && s_wasInMatch)
		OnSessionReset();
	s_wasInMatch = inMatch;

	if (busy_.load())
	{
		if (HotkeyPressed(settings.stopHotkey))
			CancelActiveJob();
		return;
	}

	if (!settings.hotkeysEnabled || !inMatch || now < s_hotkeyArmTickMs)
		return;

	if (HotkeyPressed(settings.startHotkey))
	{
		PhLog("[CAMO] Paint hotkey pressed\n");
		RequestPaint(CamoJobKind::Paint);
	}
	else if (HotkeyPressed(settings.previewHotkey))
		RequestPaint(CamoJobKind::Preview);
	else if (HotkeyPressed(settings.unpreviewHotkey))
		RequestPaint(CamoJobKind::UnPreview);
}

static bool RegionModeCombo(const char* label, int& mode)
{
	const char* items[] = { "Paint", "Fill", "Skip" };
	int idx = mode;
	if (idx < 0 || idx > 2)
		idx = 0;
	if (ImGui::Combo(label, &idx, items, IM_ARRAYSIZE(items)))
	{
		mode = idx;
		return true;
	}
	return false;
}

void CamoManager::DrawMenu()
{
	ImGui::Text("MecchaCamouflage mesh_first_paint");
	ImGui::Separator();

	const auto state = bridgeState_.load();
	const int shownPort = bridgePort_.load() > 0 ? bridgePort_.load() : kBridgePort;
	if (state == CamoBridgeState::Ready)
		ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Bridge: ready (:%d)", shownPort);
	else if (state == CamoBridgeState::Loading)
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Bridge: loading...");
	else if (state == CamoBridgeState::Error)
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Bridge: error (reopen menu to retry)");
	else
		ImGui::TextDisabled("Bridge: loading on menu open...");

	ImGui::TextWrapped("%s", StatusText());
	if (!lastError_.empty())
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", LastErrorText());

	ImGui::Separator();
	ImGui::SliderFloat("Brush 1 (texels)", &settings.brush1Texels, 10.0f, 30.0f, "%.0f");
	ImGui::SliderFloat("Brush 2 (texels)", &settings.brush2Texels, 5.0f, 10.0f, "%.0f");
	ImGui::SliderInt("Batch size", &settings.batchLimit, 1, 20);
	ImGui::SliderInt("Pacing (ms)", &settings.batchPacingMs, 50, 500);
	ImGui::SliderFloat("Side UV", &settings.sideSourceMaxUv, 0.001f, 0.08f, "%.3f");
	ImGui::SliderFloat("Front/back UV", &settings.frontBackSourceMaxUv, 0.001f, 0.45f, "%.3f");

	RegionModeCombo("Front", settings.frontRegionMode);
	RegionModeCombo("Side", settings.sideRegionMode);
	RegionModeCombo("Back", settings.backRegionMode);
	ImGui::Checkbox("Auto material", &settings.autoMaterial);

	if (!settings.autoMaterial)
	{
		ImGui::SliderFloat("Metallic", &settings.metallic, 0.0f, 1.0f);
		ImGui::SliderFloat("Roughness", &settings.roughness, 0.0f, 1.0f);
	}

	float fillRgb[3]{};
	FillColorToFloat3(settings.fillColorHex, fillRgb);
	ImVec4 fillPreview(fillRgb[0], fillRgb[1], fillRgb[2], 1.0f);
	if (ImGui::ColorButton("##fill_color_btn", fillPreview))
		ImGui::OpenPopup("popup_fill_color");
	ImGui::SameLine();
	ImGui::Text("Fill color");
	if (ImGui::BeginPopup("popup_fill_color"))
	{
		if (ImGui::ColorPicker3("##fill_color_pick", fillRgb,
			ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_DisplayRGB |
			ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_InputRGB))
			FillColorFromFloat3(fillRgb, settings.fillColorHex, IM_ARRAYSIZE(settings.fillColorHex));
		ImGui::EndPopup();
	}
	ImGui::SliderFloat("Fill metallic", &settings.fillMetallic, 0.0f, 1.0f);
	ImGui::SliderFloat("Fill roughness", &settings.fillRoughness, 0.0f, 1.0f);

	ImGui::Separator();
	const bool busy = busy_.load();
	ImGui::Checkbox("Enable camo hotkeys", &settings.hotkeysEnabled);
	if (settings.hotkeysEnabled)
		ImGui::TextDisabled("F10=start  F2=preview  F3=unpreview  F4=stop (3s after spawn)");
	else
		ImGui::TextDisabled("Hotkeys off — use buttons below, or enable above");

	ImGui::BeginDisabled(busy);
	if (ImGui::Button("Start (F10)", ImVec2(120, 0)))
		RequestPaint(CamoJobKind::Paint);
	ImGui::SameLine();
	if (ImGui::Button("Preview (F2)", ImVec2(120, 0)))
		RequestPaint(CamoJobKind::Preview);
	ImGui::SameLine();
	if (ImGui::Button("UnPreview (F3)", ImVec2(120, 0)))
		RequestPaint(CamoJobKind::UnPreview);
	ImGui::EndDisabled();

	// Stop must stay clickable while a paint/preview job is running.
	ImGui::BeginDisabled(!busy);
	if (ImGui::Button("Stop (F4)", ImVec2(120, 0)))
		CancelActiveJob();
	ImGui::EndDisabled();

	if (ImGui::Button("Save camo settings"))
		settings.Save();
}
