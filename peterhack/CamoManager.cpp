#define WIN32_LEAN_AND_MEAN
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <Windows.h>

#include "CamoManager.hpp"
#include "Log.hpp"
#include "GamepadInput.hpp"
#include "Keybinds.hpp"
#include "IconsFontAwesome6.h"
#include "Notify.hpp"
#include "Settings.hpp"

extern Settings* cfg;
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

namespace Process
{
	extern HWND Hwnd;
}

namespace
{
	constexpr int kBridgePort = 47654;
	const char* kBridgeDllName = "peterhack-bridge.dll";
	const char* kProcessName = "PenguinHotel-Win64-Shipping.exe";

	std::once_flag g_wsaOnce;
	bool g_wsaReady = false;

	// Compact JSON scalar extractors for bridge responses. The bridge emits
	// space-free JSON, so matching "key": (quote-key-quote-colon) avoids the
	// prefix collisions a bare substring search would hit (e.g. "server_batch_limit"
	// vs "server_batch_limit_effective").
	bool JsonFindInt(const std::string& s, const char* key, int& out)
	{
		std::string needle = std::string("\"") + key + "\":";
		size_t p = s.find(needle);
		if (p == std::string::npos)
			return false;
		p += needle.size();
		while (p < s.size() && s[p] == ' ')
			++p;
		bool neg = false;
		if (p < s.size() && s[p] == '-')
		{
			neg = true;
			++p;
		}
		if (p >= s.size() || !isdigit(static_cast<unsigned char>(s[p])))
			return false;
		long long v = 0;
		while (p < s.size() && isdigit(static_cast<unsigned char>(s[p])))
		{
			v = v * 10 + (s[p] - '0');
			++p;
		}
		out = static_cast<int>(neg ? -v : v);
		return true;
	}

	bool JsonFindBool(const std::string& s, const char* key, bool& out)
	{
		std::string needle = std::string("\"") + key + "\":";
		size_t p = s.find(needle);
		if (p == std::string::npos)
			return false;
		p += needle.size();
		while (p < s.size() && s[p] == ' ')
			++p;
		if (s.compare(p, 4, "true") == 0)
		{
			out = true;
			return true;
		}
		if (s.compare(p, 5, "false") == 0)
		{
			out = false;
			return true;
		}
		return false;
	}

	bool JsonFindDouble(const std::string& s, const char* key, double& out)
	{
		std::string needle = std::string("\"") + key + "\":";
		size_t p = s.find(needle);
		if (p == std::string::npos)
			return false;
		p += needle.size();
		while (p < s.size() && s[p] == ' ')
			++p;
		const size_t start = p;
		while (p < s.size() &&
			(isdigit(static_cast<unsigned char>(s[p])) || s[p] == '-' || s[p] == '+' ||
				s[p] == '.' || s[p] == 'e' || s[p] == 'E'))
			++p;
		if (p == start)
			return false;
		try
		{
			out = std::stod(s.substr(start, p - start));
		}
		catch (...)
		{
			return false;
		}
		return true;
	}

	bool JsonFindString(const std::string& s, const char* key, std::string& out)
	{
		std::string needle = std::string("\"") + key + "\":\"";
		size_t p = s.find(needle);
		if (p == std::string::npos)
			return false;
		p += needle.size();
		std::string result;
		while (p < s.size() && s[p] != '"')
		{
			if (s[p] == '\\' && p + 1 < s.size())
			{
				result.push_back(s[p + 1]);
				p += 2;
				continue;
			}
			result.push_back(s[p]);
			++p;
		}
		out = result;
		return true;
	}

	void LogPaintJobResult(CamoJobKind kind, bool ok, const std::string& response,
	                       int fallbackWireByte = -1, int fallbackWireStrokeBytes = -1)
	{
		const char* jobLabel = "paint";
		switch (kind)
		{
		case CamoJobKind::Preview: jobLabel = "preview"; break;
		case CamoJobKind::UnPreview: jobLabel = "un-preview"; break;
		default: break;
		}

		int applied = 0;
		int serverStrokes = 0;
		int localSynced = 0;
		int wireByte = -1;
		int wireStrokeBytes = -1;
		int plannedWireByte = -1;
		int actualWireByte = -1;
		double metallic = 0.0;
		double roughness = 0.0;
		double emissive = 0.0;
		double sentMetallic = 0.0;
		double sentRoughness = 0.0;
		double sentEmissive = 0.0;
		double strokeMetallic = 0.0;
		double strokeRoughness = 0.0;
		double strokeEmissive = 0.0;
		bool autoOk = false;
		std::string stage;
		std::string matSource;
		std::string matFailure;
		std::string localRoute;
		JsonFindInt(response, "applied", applied);
		JsonFindInt(response, "server_strokes_sent", serverStrokes);
		JsonFindInt(response, "local_strokes_synced", localSynced);
		JsonFindInt(response, "first_stroke_packed_wire_channel_byte", wireByte);
		JsonFindInt(response, "packed_wire_stroke_byte_size", wireStrokeBytes);
		JsonFindInt(response, "planned_first_stroke_packed_wire_channel_byte", plannedWireByte);
		JsonFindInt(response, "actual_first_stroke_packed_wire_channel_byte", actualWireByte);
		if (wireByte < 0)
			wireByte = plannedWireByte;
		if (wireByte < 0)
			wireByte = actualWireByte;
		if (wireByte < 0)
			wireByte = fallbackWireByte;
		if (wireStrokeBytes < 0)
			wireStrokeBytes = fallbackWireStrokeBytes;
		JsonFindDouble(response, "material_properties_metallic", metallic);
		JsonFindDouble(response, "material_properties_roughness", roughness);
		JsonFindDouble(response, "material_properties_emissive", emissive);
		JsonFindDouble(response, "metallic", sentMetallic);
		JsonFindDouble(response, "roughness", sentRoughness);
		JsonFindDouble(response, "emissive", sentEmissive);
		JsonFindDouble(response, "first_stroke_roughness", strokeRoughness);
		JsonFindDouble(response, "first_stroke_emissive", strokeEmissive);
		if (strokeRoughness <= 0.0)
			JsonFindDouble(response, "first_stroke_channel_data_roughness", strokeRoughness);
		if (strokeEmissive <= 0.0)
			JsonFindDouble(response, "first_stroke_channel_data_emissive", strokeEmissive);
		if (strokeMetallic <= 0.0)
			JsonFindDouble(response, "first_stroke_channel_data_metallic", strokeMetallic);
		JsonFindBool(response, "material_properties_auto_ok", autoOk);
		JsonFindString(response, "stage", stage);
		JsonFindString(response, "material_properties_source", matSource);
		JsonFindString(response, "material_properties_failure", matFailure);
		JsonFindString(response, "local_route_mode", localRoute);

		if (ok)
		{
			PhLog("[peterhack] Camo %s finished: stage=%s applied=%d server_strokes=%d local_sync=%d"
			      " wire=%d stroke_bytes=%d route=%s auto_material=%s"
			      " detected(m=%.3f r=%.3f e=%.3f) sent(m=%.3f r=%.3f e=%.3f)"
			      " stroke(m=%.3f r=%.3f e=%.3f)%s\n",
			      jobLabel,
			      stage.empty() ? "done" : stage.c_str(),
			      applied,
			      serverStrokes,
			      localSynced,
			      wireByte,
			      wireStrokeBytes,
			      localRoute.empty() ? "n/a" : localRoute.c_str(),
			      matSource.empty() ? "unknown" : matSource.c_str(),
			      metallic,
			      roughness,
			      emissive,
			      sentMetallic,
			      sentRoughness,
			      sentEmissive,
			      strokeMetallic,
			      strokeRoughness,
			      strokeEmissive,
			      autoOk ? "" : " [auto detect failed]");
			if (wireStrokeBytes == 27)
				PhLog("[peterhack] WARNING: 27-byte wire records shift world radius into emissive — fully exit game and reinject bridge rev 6+\n");
			if (wireByte == 4)
				PhLog("[peterhack] WARNING: wire channel byte 4 (All) routes roughness into emissive — update bridge (AMR channel fix)\n");
			if (wireByte == 6)
				PhLog("[peterhack] WARNING: wire channel byte 6 = Emissive-only routing — restart game to load new bridge\n");
			int wireEmissiveA = -1;
			if (JsonFindInt(response, "first_stroke_wire_emissive_a", wireEmissiveA) && wireEmissiveA > 0)
				PhLog("[peterhack] WARNING: wire emissive alpha=%d (expected 0) — reinject bridge\n", wireEmissiveA);
			if (wireByte == 5 && strokeEmissive > 0.001)
				PhLog("[peterhack] NOTE: wire channel 5 (AMR) with stroke emissive %.3f — emissive slider ignored on this channel\n",
				      strokeEmissive);
			if (!matFailure.empty() && matFailure != "ok" && !autoOk)
				PhLog("[peterhack] Material detect failure: %s\n", matFailure.c_str());
		}
		else if (response.size() <= 4096)
		{
			PhLog("[peterhack] Camo %s failed: %s\n", jobLabel, response.c_str());
		}
		else
		{
			PhLog("[peterhack] Camo %s failed (response truncated): %.4096s...\n",
			      jobLabel, response.c_str());
		}
	}

	bool EnsureWinsockInitialized()
	{
		std::call_once(g_wsaOnce, []() {
			WSADATA wsa{};
			g_wsaReady = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
		});
		return g_wsaReady;
	}

	bool IsGameWindowFocused()
	{
		const HWND fg = GetForegroundWindow();
		if (!fg)
			return false;
		if (Process::Hwnd && (fg == Process::Hwnd || IsChild(Process::Hwnd, fg)))
			return true;
		DWORD pid = 0;
		GetWindowThreadProcessId(fg, &pid);
		return pid == GetCurrentProcessId();
	}

	bool HotkeyPressed(int bind)
	{
		return Binds::Pressed(bind, true);
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

void CamoManager::UpdateDiagnostics(const std::string& response, CamoJobKind kind, bool ok)
{
	CamoDiagnostics d;
	d.valid = true;
	d.updatedMs = GetTickCount64();
	d.lastJobOk = ok;
	switch (kind)
	{
	case CamoJobKind::Preview: d.lastKind = "preview"; break;
	case CamoJobKind::UnPreview: d.lastKind = "un-preview"; break;
	case CamoJobKind::Stop: d.lastKind = "stop"; break;
	default: d.lastKind = "paint"; break;
	}
	d.configuredBatch = settings.batchLimit;
	d.configuredPacingMs = settings.batchPacingMs;

	int v = 0;
	bool b = false;
	if (JsonFindInt(response, "server_batch_limit_requested", v) ||
		JsonFindInt(response, "replication_pacing_requested_batch_limit", v))
		d.batchRequested = v;
	if (JsonFindInt(response, "server_batch_limit_effective", v) ||
		JsonFindInt(response, "replication_pacing_resolved_batch_limit", v) ||
		JsonFindInt(response, "server_batch_limit", v))
		d.batchResolved = v;
	if (JsonFindInt(response, "server_packed_batch_limit_cap", v))
		d.packedBatchCap = v;
	if (JsonFindInt(response, "local_batch_limit", v))
		d.localBatchLimit = v;
	if (JsonFindBool(response, "packed_mesh_radius_scale_used_fallback", b))
		d.radiusUsedFallback = b ? 1 : 0;
	if (JsonFindBool(response, "packed_mesh_radius_scale_within_expected_window", b))
		d.radiusWithinWindow = b ? 1 : 0;

	d.autoMaterialRequested = settings.autoMaterial;
	if (JsonFindBool(response, "material_properties_auto_ok", b))
		d.materialAutoOk = b ? 1 : 0;
	JsonFindString(response, "material_properties_source", d.materialSource);
	JsonFindString(response, "material_properties_failure", d.materialFailure);
	double dv = 0.0;
	if (JsonFindDouble(response, "material_properties_metallic", dv))
		d.materialMetallic = dv;
	if (JsonFindDouble(response, "material_properties_roughness", dv))
		d.materialRoughness = dv;
	if (JsonFindDouble(response, "material_properties_emissive", dv))
		d.materialEmissive = dv;
	d.lastFailure.clear();
	if (JsonFindInt(response, "server_strokes_sent", v))
		d.serverStrokesSent = v;
	JsonFindString(response, "paint_target_channel", d.paintTargetChannel);
	if (JsonFindInt(response, "first_stroke_packed_wire_channel_byte", v))
		d.packedWireChannelByte = v;
	if (JsonFindInt(response, "packed_wire_stroke_byte_size", v))
		d.packedWireStrokeBytes = v;
	if (JsonFindInt(response, "wire_encoding_revision", v))
		d.bridgeWireRevision = v;
	else if (bridgeWireEncodingRevision_.load() >= 0)
		d.bridgeWireRevision = bridgeWireEncodingRevision_.load();
	JsonFindString(response, "local_route_mode", d.localRouteMode);
	if (JsonFindBool(response, "local_visual_sync_degraded", b))
		d.localVisualSyncDegraded = b ? 1 : 0;
	JsonFindString(response, "first_failure", d.lastFailure);
	if (d.lastFailure.empty())
	{
		std::string stage;
		if (JsonFindString(response, "stage", stage))
			d.lastFailure = stage;
	}

	std::lock_guard<std::mutex> lock(diagMutex_);
	diag_ = d;
}

CamoManager::CamoDiagnostics CamoManager::Diagnostics() const
{
	std::lock_guard<std::mutex> lock(diagMutex_);
	return diag_;
}

void CamoManager::SeedDiagnosticsForJob(CamoJobKind kind)
{
	std::lock_guard<std::mutex> lock(diagMutex_);
	diag_.valid = true;
	diag_.updatedMs = GetTickCount64();
	diag_.lastJobOk = false;
	diag_.configuredBatch = settings.batchLimit;
	diag_.configuredPacingMs = settings.batchPacingMs;
	diag_.batchRequested = settings.batchAutoAdapt ? -1 : settings.batchLimit;
	diag_.batchResolved = -1;
	diag_.localBatchLimit = -1;
	diag_.serverStrokesSent = -1;
	diag_.paintTargetChannel.clear();
	diag_.packedWireChannelByte = -1;
	diag_.packedWireStrokeBytes = -1;
	diag_.lastFailure.clear();
	diag_.autoMaterialRequested = settings.autoMaterial;
	diag_.materialAutoOk = -1;
	switch (kind)
	{
	case CamoJobKind::Preview: diag_.lastKind = "preview"; break;
	case CamoJobKind::UnPreview: diag_.lastKind = "un-preview"; break;
	default: diag_.lastKind = "paint"; break;
	}
}

void CamoManager::RefreshDiagnosticsFromProgress()
{
	const CamoProgress pr = ReadProgress();
	if (!pr.valid)
		return;

	std::lock_guard<std::mutex> lock(diagMutex_);
	diag_.valid = true;
	diag_.updatedMs = GetTickCount64();
	if (pr.serverBatchLimit >= 0)
		diag_.batchResolved = pr.serverBatchLimit;
	if (pr.batchRequested >= 0)
		diag_.batchRequested = pr.batchRequested;
	if (pr.localBatchLimit >= 0)
		diag_.localBatchLimit = pr.localBatchLimit;
	if (pr.serverStrokesSent >= 0)
		diag_.serverStrokesSent = pr.serverStrokesSent;
	if (pr.wireChannelByte >= 0)
		diag_.packedWireChannelByte = pr.wireChannelByte;
	if (pr.wireStrokeBytes >= 0)
		diag_.packedWireStrokeBytes = pr.wireStrokeBytes;
	if (!pr.paintTargetChannel.empty())
		diag_.paintTargetChannel = pr.paintTargetChannel;
	else if (pr.paintTargetChannelValue == 4 && diag_.paintTargetChannel.empty())
		diag_.paintTargetChannel = "all";
}

namespace
{
	void PublishStatusNotify(const std::string& text)
	{
		if (!cfg || !cfg->bNotifications)
			return;
		// Only toast terminal outcomes — skip in-progress status strings that
		// update every frame or spam during bridge load.
		if (text == "Camouflage applied" || text == "Preview applied" || text == "Preview cleared")
			Notify::Success(text);
		else if (text == "Paint stop requested")
			Notify::Info(text);
	}
}

void CamoManager::SetStatus(const std::string& text)
{
	{
		std::lock_guard<std::mutex> lock(statusMutex_);
		status_ = text;
	}
	PublishStatusNotify(text);
}

void CamoManager::SetError(const std::string& text)
{
	{
		std::lock_guard<std::mutex> lock(statusMutex_);
		lastError_ = text;
		status_ = text;
	}
	if (cfg && cfg->bNotifications)
		Notify::Error(text);
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

std::string CamoManager::HudStatusLine() const
{
	const char* icon = ICON_FA_PAINTBRUSH;

	const auto bridge = bridgeState_.load();
	if (bridge == CamoBridgeState::Loading)
		return std::string(icon) + " Camo: loading bridge...";

	if (busy_.load())
	{
		// Append live percentage when the bridge is reporting progress, using the
		// same planning(0-10%)/stream(10-100%) remap as the menu bar.
		const CamoProgress pr = ReadProgress();
		char pct[16] = "";
		if (pr.valid && pr.progress > 0.0)
		{
			const bool streaming = pr.totalSteps > 4;
			float frac = streaming ? 0.10f + 0.90f * static_cast<float>(pr.progress)
			                       : 0.10f * static_cast<float>(pr.progress);
			if (frac < 0.0f) frac = 0.0f;
			if (frac > 1.0f) frac = 1.0f;
			snprintf(pct, sizeof(pct), " %.0f%%", frac * 100.0f);
		}

		switch (pendingKind_.load())
		{
		case CamoJobKind::Paint:
			return std::string(icon) + " Camo: painting..." + pct;
		case CamoJobKind::Preview:
			return std::string(icon) + " Camo: preview..." + pct;
		case CamoJobKind::UnPreview:
			return std::string(icon) + " Camo: clearing preview...";
		case CamoJobKind::Stop:
			return std::string(icon) + " Camo: stopping...";
		default:
			return std::string(icon) + " Camo: working...";
		}
	}

	std::lock_guard<std::mutex> lock(statusMutex_);
	if (status_.find("Stopping") != std::string::npos ||
		status_.find("stop requested") != std::string::npos)
		return std::string(icon) + " Camo: " + status_;

	if (status_ == "Preview applied")
		return std::string(icon) + " Camo: preview active";
	if (status_ == "Camouflage applied")
		return std::string(icon) + " Camo: paint applied";
	if (status_ == "Preview cleared")
		return std::string(icon) + " Camo: preview cleared";

	if (status_.find("failed") != std::string::npos ||
		status_.find("error") != std::string::npos ||
		status_.find("missing") != std::string::npos ||
		status_.find("not responding") != std::string::npos)
		return std::string(icon) + " Camo: " + status_;

	if (settings.hotkeysEnabled && bridge == CamoBridgeState::Ready)
	{
		char buf[80];
		snprintf(buf, sizeof(buf), "%s Camo hotkeys [%s]", icon, Binds::BindName(settings.startHotkey));
		return buf;
	}

	if (bridge == CamoBridgeState::Error)
		return std::string(icon) + " Camo: bridge error";

	return {};
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
	HMODULE mod = GetModuleHandleW(L"peterhack-bridge.dll");
	if (mod)
		return mod;
	return nullptr;
}

// The bridge writes progress next to its own loaded module as
// "<module path>.progress.json" (see write_bridge_progress in the bridge), so
// derive it from the live module handle rather than the configured DLL name.
std::wstring CamoManager::BridgeProgressPath() const
{
	HMODULE mod = GetBridgeModuleHandle();
	if (!mod)
		return {};
	wchar_t path[MAX_PATH]{};
	const DWORD n = GetModuleFileNameW(mod, path, MAX_PATH);
	if (n == 0 || n >= MAX_PATH)
		return {};
	return std::wstring(path, path + n) + L".progress.json";
}

void CamoManager::ClearProgressSidecar() const
{
	const std::wstring path = BridgeProgressPath();
	if (!path.empty())
		DeleteFileW(path.c_str());
}

CamoManager::CamoProgress CamoManager::ReadProgress() const
{
	CamoProgress out;
	const std::wstring path = BridgeProgressPath();
	if (path.empty())
		return out;

	// The bridge publishes each update atomically (write temp + MoveFileEx), so a
	// shared-read open always sees a complete document.
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE)
		return out;

	std::string text;
	LARGE_INTEGER size{};
	if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < 64 * 1024)
	{
		text.resize(static_cast<size_t>(size.QuadPart));
		DWORD read = 0;
		if (!ReadFile(f, &text[0], static_cast<DWORD>(text.size()), &read, nullptr) ||
			read != text.size())
			text.clear();
	}
	CloseHandle(f);
	if (text.empty())
		return out;

	double prog = 0.0, elapsed = 0.0;
	int step = -1, total = -1;
	int wireChannelByte = -1;
	int wireStrokeBytes = -1;
	int serverBatchLimit = -1;
	int serverStrokesSent = -1;
	int localBatchLimit = -1;
	int batchRequested = -1;
	int paintTargetChannelValue = -1;
	std::string stage, message, paintTargetChannel;
	JsonFindDouble(text, "progress", prog);
	JsonFindDouble(text, "elapsed_ms", elapsed);
	JsonFindInt(text, "step", step);
	JsonFindInt(text, "total_steps", total);
	JsonFindInt(text, "first_stroke_packed_wire_channel_byte", wireChannelByte);
	JsonFindInt(text, "packed_wire_stroke_byte_size", wireStrokeBytes);
	JsonFindInt(text, "server_batch_limit", serverBatchLimit);
	JsonFindInt(text, "server_strokes_sent", serverStrokesSent);
	JsonFindInt(text, "local_batch_limit", localBatchLimit);
	JsonFindInt(text, "replication_pacing_requested_batch_limit", batchRequested);
	if (batchRequested < 0)
		JsonFindInt(text, "server_batch_limit_requested", batchRequested);
	JsonFindInt(text, "paint_target_channel_value", paintTargetChannelValue);
	JsonFindString(text, "paint_target_channel", paintTargetChannel);
	JsonFindString(text, "stage", stage);
	JsonFindString(text, "message", message);

	out.valid = true;
	out.progress = prog < 0.0 ? 0.0 : (prog > 1.0 ? 1.0 : prog);
	out.elapsedMs = elapsed;
	out.step = step;
	out.totalSteps = total;
	out.wireChannelByte = wireChannelByte;
	out.wireStrokeBytes = wireStrokeBytes;
	out.serverBatchLimit = serverBatchLimit;
	out.serverStrokesSent = serverStrokesSent;
	out.localBatchLimit = localBatchLimit;
	out.batchRequested = batchRequested;
	out.paintTargetChannelValue = paintTargetChannelValue;
	out.paintTargetChannel = paintTargetChannel;
	out.stage = stage;
	out.message = message;
	out.terminal = stage.find("done") != std::string::npos ||
		stage.find("failed") != std::string::npos ||
		stage.find("cancelled") != std::string::npos;
	return out;
}

bool CamoManager::BridgeModuleLoaded() const
{
	return GetBridgeModuleHandle() != nullptr;
}

bool CamoManager::TryReloadBridgeModuleIfDiskUpdated()
{
	HMODULE mod = GetBridgeModuleHandle();
	if (!mod)
		return false;

	wchar_t loadedPath[MAX_PATH]{};
	if (GetModuleFileNameW(mod, loadedPath, MAX_PATH) == 0)
		return false;

	const std::wstring diskPath = ResolveBridgeDllPath();
	WIN32_FILE_ATTRIBUTE_DATA loadedInfo{};
	WIN32_FILE_ATTRIBUTE_DATA diskInfo{};
	if (!GetFileAttributesExW(loadedPath, GetFileExInfoStandard, &loadedInfo) ||
		!GetFileAttributesExW(diskPath.c_str(), GetFileExInfoStandard, &diskInfo))
	{
		return false;
	}

	ULARGE_INTEGER loadedTime{};
	ULARGE_INTEGER diskTime{};
	loadedTime.LowPart = loadedInfo.ftLastWriteTime.dwLowDateTime;
	loadedTime.HighPart = loadedInfo.ftLastWriteTime.dwHighDateTime;
	diskTime.LowPart = diskInfo.ftLastWriteTime.dwLowDateTime;
	diskTime.HighPart = diskInfo.ftLastWriteTime.dwHighDateTime;

	// One second tolerance for filesystem timestamp granularity.
	if (diskTime.QuadPart <= loadedTime.QuadPart + 10'000'000ULL)
		return false;

	PhLog("[peterhack] Bridge DLL on disk is newer than the loaded module — reloading\n");
	PhLog("[peterhack]   loaded: %ls\n", loadedPath);
	PhLog("[peterhack]   disk:   %ls\n", diskPath.c_str());

	InvalidateBridgeConnection();
	if (bridgePort_.load() > 0 && bridgeUsesHello_.load())
		RequestBridgeShutdown(8000);
	Sleep(500);

	if (!FreeLibrary(mod))
	{
		PhLog("[peterhack] FreeLibrary(peterhack-bridge.dll) failed err=%lu — fully exit game and relaunch\n",
		      static_cast<unsigned long>(GetLastError()));
		return false;
	}

	ClearBridgeIdentity();
	bridgeState_.store(CamoBridgeState::Unloaded);
	bridgeWireEncodingRevision_.store(-1);

	if (!LoadLibraryW(diskPath.c_str()))
	{
		PhLog("[peterhack] Reload LoadLibrary failed err=%lu\n", static_cast<unsigned long>(GetLastError()));
		return false;
	}

	PhLog("[peterhack] Reloaded peterhack-bridge.dll from disk\n");
	return true;
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
	{
		PhLog("[BRIDGE] TCP rejected — invalid port %d\n", port);
		return false;
	}

	if (!EnsureWinsockInitialized())
	{
		PhLog("[BRIDGE] TCP rejected — Winsock unavailable\n");
		return false;
	}

	const std::string reqType = PhJsonTypeField(requestJson);
	PhLogJsonLine("BRIDGE", "->", requestJson);

	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
	{
		PhLog("[BRIDGE] socket() failed err=%d\n", WSAGetLastError());
		return false;
	}

	DWORD connectTimeout = static_cast<DWORD>(timeoutMs > 1500 ? 1500 : timeoutMs);
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&connectTimeout), sizeof(connectTimeout));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&connectTimeout), sizeof(connectTimeout));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(static_cast<u_short>(port));
	inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

	if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
	{
		PhLog("[BRIDGE] connect 127.0.0.1:%d failed type=%s err=%d\n", port, reqType.c_str(), WSAGetLastError());
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
			PhLog("[BRIDGE] hello handshake failed type=%s port=%d\n", reqType.c_str(), port);
			closesocket(sock);
			return false;
		}
		PhLogJsonLine("BRIDGE", "<-hello", helloResponse, 2048);
		if (helloResponse.find("\"success\":true") == std::string::npos ||
			helloResponse.find("\"stage\":\"hello\"") == std::string::npos)
		{
			PhLog("[BRIDGE] hello rejected type=%s port=%d\n", reqType.c_str(), port);
			closesocket(sock);
			return false;
		}
	}

	if (!sendLine(requestJson))
	{
		PhLog("[BRIDGE] send failed type=%s port=%d err=%d\n", reqType.c_str(), port, WSAGetLastError());
		closesocket(sock);
		return false;
	}

	if (!recvLine(responseOut))
	{
		PhLog("[BRIDGE] recv failed type=%s port=%d err=%d timeout=%dms\n",
		      reqType.c_str(), port, WSAGetLastError(), timeoutMs);
		closesocket(sock);
		return false;
	}

	closesocket(sock);
	PhLogJsonLine("BRIDGE", "<-", responseOut);
	if (!PhJsonSuccessField(responseOut))
		PhLog("[BRIDGE] response success=false type=%s port=%d\n", reqType.c_str(), port);
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

	PhLog("[peterhack] BridgeStartV1 failed exit=%lu state=%u win32=%lu winsock=%lu\n",
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
		PhLog("[peterhack] Using legacy bridge listener on 127.0.0.1:%d\n", kBridgePort);
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
		PhLog("[peterhack] Bridge ping failed — requesting shutdown before restart\n");
		if (!RequestBridgeShutdown(12000))
			PhLog("[peterhack] Bridge shutdown timed out — retrying start\n");
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
				PhLog("[peterhack] Reconnected to existing legacy bridge on port %u\n", block.bound_port);
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
	PhLog("[peterhack] BridgeStartV1 listening on 127.0.0.1:%u\n", block.bound_port);
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

		PhLog("[peterhack] Bridge marked ready but TCP ping failed — restarting listener\n");
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

	if (BridgeModuleLoaded())
		TryReloadBridgeModuleIfDiskUpdated();

	if (!BridgeModuleLoaded())
	{
		const std::wstring root = ResolveBridgeRoot();
		_wmkdir((DesktopDeployDir() + L"\\bridge").c_str());
		_wmkdir(root.c_str());

		const std::wstring dllPath = ResolveBridgeDllPath();
		if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			bridgeState_.store(CamoBridgeState::Error);
			SetError("Bridge DLL missing — build with build.bat or copy peterhack-bridge.dll next to peterhack.dll");
			PhLog("[peterhack] Bridge DLL missing at expected path\n");
			return false;
		}

		if (!LoadLibraryW(dllPath.c_str()))
		{
			bridgeState_.store(CamoBridgeState::Error);
			SetError("LoadLibrary failed for peterhack-bridge.dll");
			PhLog("[peterhack] LoadLibrary failed for peterhack-bridge.dll\n");
			return false;
		}
		PhLog("[peterhack] Loaded peterhack-bridge.dll\n");
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
					PhLog("[peterhack] Bridge missing mesh_first_paint capability\n");
					return false;
				}
				int wireRev = -1;
				int wireStrokeBytes = -1;
				int channelDataSize = 0;
				if (JsonFindInt(caps, "wire_encoding_revision", wireRev))
				{
					bridgeWireEncodingRevision_.store(wireRev);
					PhLog("[peterhack] Bridge wire_encoding_revision=%d\n", wireRev);
				}
				else
				{
					bridgeWireEncodingRevision_.store(0);
					PhLog("[peterhack] Bridge missing wire_encoding_revision — fully restart game to load new bridge\n");
				}
				if (JsonFindInt(caps, "packed_wire_stroke_byte_size", wireStrokeBytes))
					PhLog("[peterhack] Bridge packed_wire_stroke_byte_size=%d\n", wireStrokeBytes);
				if (JsonFindInt(caps, "paint_channel_data_size", channelDataSize))
					PhLog("[peterhack] Bridge paint_channel_data_size=0x%X\n", channelDataSize);
				if (wireRev >= 0 && wireRev < 6)
					PhLog("[peterhack] WARNING: bridge wire_encoding_revision=%d is outdated — restart game after rebuild\n", wireRev);
				if (wireStrokeBytes == 27)
					PhLog("[peterhack] WARNING: bridge reports 27-byte wire — emissive glow bug — restart game\n");
				if (channelDataSize > 0 && channelDataSize != 0x24)
					PhLog("[peterhack] WARNING: bridge paint_channel_data_size=0x%X expected 0x24 — restart game\n", channelDataSize);
				PhLog("[peterhack] Bridge ready on 127.0.0.1:%d\n", bridgePort_.load());
			}
			MarkBridgeReady(bridgePort_.load());
			SetStatus("Camo bridge ready");
			return true;
		}
		Sleep(125);
	}

	bridgeState_.store(CamoBridgeState::Error);
	SetError("Bridge TCP not responding after start");
	PhLog("[peterhack] Bridge TCP not responding on 127.0.0.1:%d\n", bridgePort_.load());
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
	CamoSettings paintSettings = settings;
	paintSettings.ClampLimits();
	int r = 255, g = 255, b = 255;
	ParseHexColor(paintSettings.fillColorHex, r, g, b);
	const double fr = r / 255.0;
	const double fg = g / 255.0;
	const double fb = b / 255.0;
	const float strokeEmissive = paintSettings.emissive;
	const float fillEmissiveSent = paintSettings.fillEmissive;

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
		"\"brush_1_enabled\":%s,"
		"\"brush_1_size_texels\":%.3f,"
		"\"brush_2_enabled\":%s,"
		"\"brush_2_size_texels\":%.3f,"
		"\"server_batch_auto_adapt\":%s,"
		"\"server_batch_limit\":%d,"
		"\"server_batch_pacing_ms\":%d,"
		"\"coverage_step_texels\":%.3f,"
		"\"side_source_max_uv\":%.4f,"
		"\"front_back_source_max_uv\":%.4f,"
		"\"auto_material\":%s,"
		"\"metallic\":%.3f,"
		"\"roughness\":%.3f,"
		"\"emissive\":%.3f,"
		"\"front_region_mode\":\"%s\","
		"\"side_region_mode\":\"%s\","
		"\"back_region_mode\":\"%s\","
		"\"fill_color\":\"%s\","
		"\"fill_color_r\":%.8f,"
		"\"fill_color_g\":%.8f,"
		"\"fill_color_b\":%.8f,"
		"\"fill_metallic\":%.3f,"
		"\"fill_roughness\":%.3f,"
		"\"fill_emissive\":%.3f"
		"}}",
		preview ? "true" : "false",
		unpreview ? "true" : "false",
		static_cast<unsigned long>(pid),
		kProcessName,
		paintSettings.brush1Enabled ? "true" : "false",
		paintSettings.brush1Texels,
		paintSettings.brush2Enabled ? "true" : "false",
		paintSettings.brush2Texels,
		paintSettings.batchAutoAdapt ? "true" : "false",
		paintSettings.batchLimit,
		paintSettings.batchPacingMs,
		paintSettings.CoverageStepTexels(),
		paintSettings.sideSourceMaxUv,
		paintSettings.frontBackSourceMaxUv,
		paintSettings.autoMaterial ? "true" : "false",
		paintSettings.metallic,
		paintSettings.roughness,
		strokeEmissive,
		RegionModeJson(paintSettings.frontRegionMode),
		RegionModeJson(paintSettings.sideRegionMode),
		RegionModeJson(paintSettings.backRegionMode),
		paintSettings.fillColorHex,
		fr, fg, fb,
		paintSettings.fillMetallic,
		paintSettings.fillRoughness,
		fillEmissiveSent);
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
	{
		PhLog("[CAMO] RunJob aborted — bridge unavailable (kind=%d)\n", static_cast<int>(kind));
		return false;
	}

	const DWORD pid = GetCurrentProcessId();
	const std::string payload = BuildPaintPayload(pid, kind);
	PhLog("[CAMO] RunJob kind=%d pid=%lu autoMaterial=%d metallic=%.3f roughness=%.3f emissive=%.3f fillEmissive=%.3f payloadBytes=%zu\n",
	      static_cast<int>(kind),
	      static_cast<unsigned long>(pid),
	      settings.autoMaterial ? 1 : 0,
	      settings.metallic,
	      settings.roughness,
	      settings.emissive,
	      settings.fillEmissive,
	      payload.size());
	std::string response;
	const int timeoutMs = (kind == CamoJobKind::UnPreview || kind == CamoJobKind::Preview) ? 120000 : 600000;
	if (!TcpRequest(payload, response, timeoutMs))
	{
		SetError("Bridge paint request failed (TCP timeout)");
		return false;
	}

	if (response.find("\"success\":true") == std::string::npos)
	{
		UpdateDiagnostics(response, kind, false);
		RefreshDiagnosticsFromProgress();
		{
			const auto diag = Diagnostics();
			LogPaintJobResult(kind, false, response, diag.packedWireChannelByte, diag.packedWireStrokeBytes);
		}
		if (response.find("\"stage\":\"mesh_paint_cancelled\"") != std::string::npos)
			SetStatus("Paint stopped (partial apply may remain visible)");
		else if (response.find("\"stage\":\"planner_blocked\"") != std::string::npos)
			SetError("Camo blocked — pose too extreme (e.g. emote clipping into wall). Stand normally or use fill-only regions.");
		else
			SetError("Paint failed — see console");
		return false;
	}

	UpdateDiagnostics(response, kind, true);
	RefreshDiagnosticsFromProgress();
	{
		const auto diag = Diagnostics();
		LogPaintJobResult(kind, true, response, diag.packedWireChannelByte, diag.packedWireStrokeBytes);
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
	SeedDiagnosticsForJob(kind);
	// Drop any leftover sidecar from a previous run so the progress bar starts
	// fresh at 0% instead of flashing the last job's final percentage.
	ClearProgressSidecar();
	switch (kind)
	{
	case CamoJobKind::Paint:
		SetStatus("Painting camouflage...");
		break;
	case CamoJobKind::Preview:
		SetStatus("Applying preview...");
		break;
	case CamoJobKind::UnPreview:
		SetStatus("Clearing preview...");
		break;
	default:
		break;
	}
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
		Binds::SyncKeyState(vk);
}

void CamoManager::TickHotkeys(bool inMatch, bool menuOpen)
{
	if (menuOpen)
		return;

	if (!IsGameWindowFocused())
		return;

	static bool s_wasInMatch = false;
	static ULONGLONG s_hotkeyArmTickMs = 0;
	const ULONGLONG now = GetTickCount64();
	if (inMatch && !s_wasInMatch)
	{
		ClearHotkeyEdges();
		s_hotkeyArmTickMs = now + 100;
		PhLog("[peterhack] Match entered — camo hotkeys armed in 0.1s (enable + save in Camo tab if needed)\n");
		if (settings.hotkeysEnabled && bridgeState_.load() != CamoBridgeState::Ready)
			StartBridgeLoadAsync();
	}
	if (!inMatch && s_wasInMatch)
		OnSessionReset();
	s_wasInMatch = inMatch;

	if (busy_.load())
	{
		RefreshDiagnosticsFromProgress();
		if (HotkeyPressed(settings.stopHotkey))
			CancelActiveJob();
		// Drain other bind edges so they don't fire the moment paint finishes.
		(void)HotkeyPressed(settings.startHotkey);
		(void)HotkeyPressed(settings.previewHotkey);
		(void)HotkeyPressed(settings.unpreviewHotkey);
		return;
	}

	if (!settings.hotkeysEnabled || !inMatch || now < s_hotkeyArmTickMs)
		return;

	if (HotkeyPressed(settings.startHotkey))
	{
		PhLog("[peterhack] Paint hotkey pressed\n");
		RequestPaint(CamoJobKind::Paint);
	}
	else if (HotkeyPressed(settings.previewHotkey))
		RequestPaint(CamoJobKind::Preview);
	else if (HotkeyPressed(settings.unpreviewHotkey))
		RequestPaint(CamoJobKind::UnPreview);
	else if (HotkeyPressed(settings.stopHotkey))
		CancelActiveJob();
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
	const bool busy = busy_.load();

	ImGui::Text("Peterhack mesh-first paint");
	ImGui::Separator();

	const auto state = bridgeState_.load();
	const int shownPort = bridgePort_.load() > 0 ? bridgePort_.load() : kBridgePort;
	if (state == CamoBridgeState::Ready)
	{
		ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Bridge: ready (:%d)", shownPort);
		const int wireRev = bridgeWireEncodingRevision_.load();
		if (wireRev >= 6)
			ImGui::TextDisabled("Wire encoding revision: %d (31-byte packed PBR)", wireRev);
		else if (wireRev >= 1)
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
				ICON_FA_TRIANGLE_EXCLAMATION " Wire rev %d outdated (27-byte emissive bug) — fully exit game and reinject", wireRev);
		else if (wireRev == 0)
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
				ICON_FA_TRIANGLE_EXCLAMATION " Bridge stale — fully exit game, reinject, then UnPreview");
	}
	else if (state == CamoBridgeState::Loading)
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Bridge: loading...");
	else if (state == CamoBridgeState::Error)
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Bridge: error (reopen menu to retry)");
	else
		ImGui::TextDisabled("Bridge: loading on menu open...");

	ImGui::TextWrapped("%s", StatusText());
	if (!lastError_.empty())
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", LastErrorText());

	// Live progress bar while a paint/preview job runs. The bar fills as the
	// bridge streams the paint; for a full paint it only reaches 100% once the
	// server batch stream completes, which is when other players see it done.
	if (busy)
	{
		const CamoProgress pr = ReadProgress();
		const CamoJobKind kind = pendingKind_.load();

		// The bridge reports two kinds of steps: quick planning stages (total==4)
		// and the long server-batch stream (total==stroke count, i.e. > 4).
		// Remap so the bar stays monotonic: planning -> 0..10%, stream -> 10..100%.
		const bool streaming = pr.valid && pr.totalSteps > 4;
		float frac = 0.0f;
		if (pr.valid)
			frac = streaming ? 0.10f + 0.90f * static_cast<float>(pr.progress)
			                 : 0.10f * static_cast<float>(pr.progress);
		if (frac < 0.0f) frac = 0.0f;
		if (frac > 1.0f) frac = 1.0f;

		char overlay[48];
		if (streaming && pr.totalSteps > 0)
			snprintf(overlay, sizeof(overlay), "%.0f%% (%d / %d)", frac * 100.0f, pr.step, pr.totalSteps);
		else
			snprintf(overlay, sizeof(overlay), "%.0f%%", frac * 100.0f);
		ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f), overlay);

		if (pr.valid && !pr.message.empty())
			ImGui::TextDisabled("%s", pr.message.c_str());
		else if (streaming && pr.totalSteps > 0)
			ImGui::TextDisabled("%d / %d strokes", pr.step, pr.totalSteps);

		// ETA is only meaningful during the streamed server phase - that phase's
		// completion is exactly when other players see the paint fully applied.
		if (streaming && pr.progress > 0.02 && pr.progress < 0.999 && pr.elapsedMs > 0.0)
		{
			const double etaSec = ((pr.elapsedMs / pr.progress) - pr.elapsedMs) / 1000.0;
			if (kind == CamoJobKind::Paint)
				ImGui::Text(ICON_FA_GAUGE_HIGH " ~%.1fs until others see it fully", etaSec);
			else
				ImGui::Text(ICON_FA_GAUGE_HIGH " ~%.1fs remaining", etaSec);
		}
		else if (streaming && pr.progress >= 0.999)
		{
			ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Finalizing — others see it now.");
		}
		else if (kind == CamoJobKind::Paint)
		{
			ImGui::TextDisabled("Preparing paint...");
		}
	}

	ImGui::Separator();
	ImGui::Checkbox("Brush 1", &settings.brush1Enabled);
	if (settings.brush1Enabled)
		ImGui::SliderFloat("Brush 1 (texels)", &settings.brush1Texels, 10.0f, 50.0f, "%.0f");
	ImGui::Checkbox("Brush 2", &settings.brush2Enabled);
	if (settings.brush2Enabled)
		ImGui::SliderFloat("Brush 2 (texels)", &settings.brush2Texels, 1.0f, 10.0f, "%.0f");
	if (!settings.brush1Enabled && !settings.brush2Enabled)
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Enable at least one brush");
	ImGui::TextDisabled("Coverage step: %.0f texels", settings.CoverageStepTexels());
	ImGui::Checkbox("Auto adapt batch/pacing", &settings.batchAutoAdapt);
	if (settings.batchAutoAdapt)
		ImGui::TextDisabled("Bridge picks batch size and pacing from queue pressure");
	else
	{
		ImGui::SliderInt("Batch size", &settings.batchLimit, 1, 500);
		if (settings.batchLimit > 20)
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
				ICON_FA_TRIANGLE_EXCLAMATION " Batch > 20 is experimental (net overflow risk)");
		ImGui::SliderInt("Pacing (ms)", &settings.batchPacingMs, 1, 500);
		if (settings.batchPacingMs < 50)
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
				ICON_FA_TRIANGLE_EXCLAMATION " Pacing < 50ms is faster but may throttle or drop strokes online");
	}
	ImGui::SliderFloat("Side UV", &settings.sideSourceMaxUv, 0.001f, 0.50f, "%.3f");
	ImGui::SliderFloat("Front/back UV", &settings.frontBackSourceMaxUv, 0.001f, 2.0f, "%.3f");

	ImGui::Separator();
	ImGui::Checkbox("Auto material", &settings.autoMaterial);
	if (settings.autoMaterial)
	{
		ImGui::TextDisabled("Reads metallic, roughness, and emissive from the target's existing paint");
		ImGui::TextDisabled("Manual values below are fallbacks if auto detect fails");
	}
	if (settings.autoMaterial)
		ImGui::BeginDisabled();
	ImGui::SliderFloat("Metallic", &settings.metallic, 0.0f, 1.0f);
	ImGui::SliderFloat("Roughness", &settings.roughness, 0.0f, 1.0f);
	ImGui::SliderFloat("Emissive", &settings.emissive, 0.0f, 1.0f);
	if (settings.autoMaterial)
		ImGui::EndDisabled();
	if (ImGui::Button("Reset to in-game quality defaults"))
	{
		settings.ApplyDefaults();
		Notify::Info("Camo reset to in-game quality defaults");
	}

	ImGui::Separator();
	RegionModeCombo("Front", settings.frontRegionMode);
	RegionModeCombo("Side", settings.sideRegionMode);
	RegionModeCombo("Back", settings.backRegionMode);

	const bool usesFill = settings.UsesFill();
	if (usesFill)
	{
		ImGui::Separator();
		ImGui::Text("Fill");
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
		ImGui::SliderFloat("Fill emissive", &settings.fillEmissive, 0.0f, 1.0f);
	}
	else
	{
		ImGui::Separator();
		ImGui::TextDisabled("Fill settings (enable Fill on a region)");
	}

	ImGui::Separator();
	ImGui::Text("Presets");
	static char camoPresetName[64] = "";
	static int selectedCamoPreset = -1;
	static std::vector<std::string> camoPresets;
	static bool camoPresetsLoaded = false;
	if (!camoPresetsLoaded)
	{
		camoPresets = CamoSettings::ListPresets();
		camoPresetsLoaded = true;
	}

	ImGui::SetNextItemWidth(200.0f);
	ImGui::InputTextWithHint("##camopresetname", "new preset name", camoPresetName, sizeof(camoPresetName));
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save Preset") && camoPresetName[0] != '\0')
	{
		if (settings.SavePreset(camoPresetName))
			Notify::Success(std::string("Saved camo preset: ") + camoPresetName);
		else
			Notify::Error("Save camo preset failed");
		camoPresets = CamoSettings::ListPresets();
	}

	const char* camoPreview = (selectedCamoPreset >= 0 && selectedCamoPreset < (int)camoPresets.size())
		? camoPresets[selectedCamoPreset].c_str()
		: "Select preset";
	ImGui::SetNextItemWidth(200.0f);
	if (ImGui::BeginCombo("##camopresetcombo", camoPreview))
	{
		for (int i = 0; i < (int)camoPresets.size(); i++)
		{
			const bool sel = (i == selectedCamoPreset);
			if (ImGui::Selectable(camoPresets[i].c_str(), sel))
				selectedCamoPreset = i;
			if (sel)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	const bool haveCamoPreset = selectedCamoPreset >= 0 && selectedCamoPreset < (int)camoPresets.size();
	ImGui::BeginDisabled(!haveCamoPreset);
	if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load##camopreset") && haveCamoPreset)
	{
		if (settings.LoadPreset(camoPresets[selectedCamoPreset]))
		{
			settings.Save();
			Notify::Success(std::string("Loaded preset: ") + camoPresets[selectedCamoPreset]);
		}
		else
			Notify::Error("Load camo preset failed");
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(busy);
	if (ImGui::Button(ICON_FA_PAINTBRUSH " Apply##camopreset") && haveCamoPreset)
	{
		if (settings.LoadPreset(camoPresets[selectedCamoPreset]))
		{
			settings.Save();
			RequestPaint(CamoJobKind::Paint);
			Notify::Info(std::string("Applying preset: ") + camoPresets[selectedCamoPreset]);
		}
		else
			Notify::Error("Apply camo preset failed");
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_TRASH_CAN " Delete##camopreset") && haveCamoPreset)
	{
		const std::string deleted = camoPresets[selectedCamoPreset];
		if (CamoSettings::DeletePreset(deleted))
		{
			Notify::Warn(std::string("Deleted preset: ") + deleted);
			camoPresets = CamoSettings::ListPresets();
			selectedCamoPreset = -1;
		}
		else
			Notify::Error("Delete camo preset failed");
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	if (ImGui::Checkbox("Enable camo hotkeys", &settings.hotkeysEnabled))
	{
		settings.Save();
		if (settings.hotkeysEnabled)
		{
			ClearHotkeyEdges();
			if (bridgeState_.load() != CamoBridgeState::Ready)
				StartBridgeLoadAsync();
		}
	}
	if (settings.hotkeysEnabled)
	{
		// Custom keybind recorder: click a bind, then press any key — or any
		// controller button when Controller Binds is enabled in the Misc tab.
		const bool padAllowed = Gamepad::IsEnabled();
		bool bindChanged = false;
		bindChanged |= Binds::RecorderRow("Start", settings.startHotkey, padAllowed, false);
		bindChanged |= Binds::RecorderRow("Preview", settings.previewHotkey, padAllowed, false);
		bindChanged |= Binds::RecorderRow("UnPreview", settings.unpreviewHotkey, padAllowed, false);
		bindChanged |= Binds::RecorderRow("Stop", settings.stopHotkey, padAllowed, false);
		if (bindChanged)
			settings.Save();
		if (!padAllowed)
			ImGui::TextDisabled("Enable Controller Binds (Misc tab) to record pad buttons");
		ImGui::TextDisabled("Active in match with the menu closed and game focused");
	}
	else
		ImGui::TextDisabled("Hotkeys off — use buttons below, or enable above");

	if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save camo settings"))
	{
		settings.Save();
		Notify::Success("Camo settings saved");
	}

	char startLabel[48], previewLabel[48], unpreviewLabel[48], stopLabel[48];
	snprintf(startLabel, sizeof(startLabel), "Start (%s)", Binds::BindName(settings.startHotkey));
	snprintf(previewLabel, sizeof(previewLabel), "Preview (%s)", Binds::BindName(settings.previewHotkey));
	snprintf(unpreviewLabel, sizeof(unpreviewLabel), "UnPreview (%s)", Binds::BindName(settings.unpreviewHotkey));
	snprintf(stopLabel, sizeof(stopLabel), "Stop (%s)", Binds::BindName(settings.stopHotkey));

	// Left column: Start / Stop.  Right column: Preview / UnPreview
	// (UnPreview sits directly below Preview).
	ImGui::BeginDisabled(busy);
	if (ImGui::Button(startLabel, ImVec2(120, 0)))
		RequestPaint(CamoJobKind::Paint);
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(busy);
	if (ImGui::Button(previewLabel, ImVec2(120, 0)))
		RequestPaint(CamoJobKind::Preview);
	ImGui::EndDisabled();

	// Stop must stay clickable while a paint/preview job is running.
	ImGui::BeginDisabled(!busy);
	if (ImGui::Button(stopLabel, ImVec2(120, 0)))
		CancelActiveJob();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(busy);
	if (ImGui::Button(unpreviewLabel, ImVec2(120, 0)))
		RequestPaint(CamoJobKind::UnPreview);
	ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::Checkbox(ICON_FA_CHART_LINE " Show net diagnostics", &settings.showDiagnostics);
	if (settings.showDiagnostics)
	{
		RefreshDiagnosticsFromProgress();
		DrawDiagnostics();
	}
}

void CamoManager::DrawDiagnostics()
{
	const ImVec4 kWarn(1.0f, 0.75f, 0.2f, 1.0f);
	const ImVec4 kOk(0.3f, 1.0f, 0.4f, 1.0f);
	const ImVec4 kBad(1.0f, 0.35f, 0.35f, 1.0f);

	const CamoDiagnostics d = Diagnostics();

	ImGui::TextDisabled(ICON_FA_GAUGE_HIGH " Replication / pacing diagnostics");
	if (busy_.load())
		ImGui::TextDisabled("Live — updates while paint runs (hotkey or button)");
	if (!d.valid)
	{
		ImGui::TextDisabled("No paint run yet — Start or Preview to collect data.");
		return;
	}

	ImGui::Text("Configured: batch %d, pacing %dms", d.configuredBatch, d.configuredPacingMs);
	if (d.configuredBatch > 20)
		ImGui::TextColored(kWarn, ICON_FA_TRIANGLE_EXCLAMATION " Batch > 20 is experimental");

	if (d.packedBatchCap >= 0)
		ImGui::Text("Server packed cap: %d", d.packedBatchCap);

	if (d.batchRequested >= 0 || d.batchResolved >= 0)
	{
		char reqBuf[16], resBuf[16];
		if (d.batchRequested >= 0)
			snprintf(reqBuf, sizeof(reqBuf), "%d", d.batchRequested);
		else
			snprintf(reqBuf, sizeof(reqBuf), "n/a");
		if (d.batchResolved >= 0)
			snprintf(resBuf, sizeof(resBuf), "%d", d.batchResolved);
		else
			snprintf(resBuf, sizeof(resBuf), "n/a");
		ImGui::Text("Batch requested: %s  resolved: %s", reqBuf, resBuf);

		if (d.batchRequested >= 0 && d.batchResolved >= 0 && d.batchResolved < d.batchRequested)
			ImGui::TextColored(kWarn, ICON_FA_TRIANGLE_EXCLAMATION " Server throttled batch (pacing pressure)");
		else if (d.batchRequested >= 0 && d.batchResolved >= 0)
			ImGui::TextColored(kOk, ICON_FA_CIRCLE_CHECK " Batch accepted at requested size");
	}

	if (d.localBatchLimit >= 0)
		ImGui::Text("Local batch limit: %d", d.localBatchLimit);

	if (!d.localRouteMode.empty())
	{
		ImGui::Text("Local route: %s", d.localRouteMode.c_str());
		if (d.localVisualSyncDegraded == 1)
			ImGui::TextColored(kWarn, ICON_FA_TRIANGLE_EXCLAMATION
				" Local texture mirror degraded — server paint still runs");
	}

	if (d.serverStrokesSent >= 0 && busy_.load())
		ImGui::Text("Server strokes sent: %d", d.serverStrokesSent);

	if (d.radiusUsedFallback == 1)
		ImGui::TextColored(kWarn, ICON_FA_TRIANGLE_EXCLAMATION " Radius calibration used fallback");
	else if (d.radiusWithinWindow == 0)
		ImGui::TextColored(kWarn, ICON_FA_TRIANGLE_EXCLAMATION " Radius scale outside expected window");
	else if (d.radiusWithinWindow == 1)
		ImGui::TextColored(kOk, ICON_FA_CIRCLE_CHECK " Radius calibration nominal");

	if (!d.paintTargetChannel.empty() || d.packedWireChannelByte >= 0)
	{
		ImGui::Separator();
		ImGui::TextDisabled("Paint channel");
		if (!d.paintTargetChannel.empty())
			ImGui::Text("Target: %s", d.paintTargetChannel.c_str());
		if (d.packedWireChannelByte >= 0)
		{
			const int wireRev = d.bridgeWireRevision >= 0 ? d.bridgeWireRevision : bridgeWireEncodingRevision_.load();
			const bool latestWire = d.packedWireStrokeBytes == 31 || wireRev >= 6;
			if (d.packedWireChannelByte == 5 && latestWire)
				ImGui::TextColored(kOk, ICON_FA_CIRCLE_CHECK
					" Wire channel byte: %d (albedo + metallic + roughness)", d.packedWireChannelByte);
			else if (d.packedWireChannelByte == 6 && latestWire)
				ImGui::TextColored(kBad, ICON_FA_CIRCLE_XMARK
					" Wire channel byte: %d (EMISSIVE enum — stale bridge)", d.packedWireChannelByte);
			else if (d.packedWireStrokeBytes == 27 && latestWire)
				ImGui::TextColored(kBad, ICON_FA_CIRCLE_XMARK
					" Wire stroke bytes: 27 (world radius written into emissive slot — reinject)");
			else if (d.packedWireChannelByte == 6 && d.packedWireStrokeBytes == 31)
				ImGui::TextColored(kBad, ICON_FA_CIRCLE_XMARK
					" Wire channel byte: %d (legacy emissive routing — fully restart game)", d.packedWireChannelByte);
			else
				ImGui::Text("Wire channel byte: %d", d.packedWireChannelByte);
		}
		if (d.packedWireStrokeBytes >= 0)
			ImGui::Text("Wire stroke bytes: %d%s", d.packedWireStrokeBytes,
				d.packedWireStrokeBytes == 31 ? " (correct)" :
				d.packedWireStrokeBytes == 27 ? " (emissive slot bug — reinject)" : " (unexpected)");
	}
	else if (settings.showDiagnostics)
	{
		ImGui::Separator();
		ImGui::TextDisabled("Paint channel");
		const int wireRev = d.bridgeWireRevision >= 0 ? d.bridgeWireRevision : bridgeWireEncodingRevision_.load();
		if (wireRev >= 0 && wireRev < 6)
			ImGui::TextColored(kBad, ICON_FA_TRIANGLE_EXCLAMATION
				" Stale bridge (rev %d) — fully exit game and reinject.", wireRev);
		else
			ImGui::TextDisabled("Waiting for paint channel data from bridge progress/response");
	}

	if (!d.lastFailure.empty())
	{
		if (d.lastFailure.find("mesh_paint_cancelled") != std::string::npos)
		{
			if (d.serverStrokesSent >= 0)
				ImGui::TextColored(kWarn, ICON_FA_TRIANGLE_EXCLAMATION " You stopped early — %d server strokes were already sent",
					d.serverStrokesSent);
			else
				ImGui::TextColored(kWarn, ICON_FA_TRIANGLE_EXCLAMATION " You stopped early (partial apply may show emissive until UnPreview)");
		}
		else if (!d.lastJobOk)
			ImGui::TextColored(kBad, ICON_FA_CIRCLE_XMARK " Failure: %s", d.lastFailure.c_str());
	}

	ImGui::Separator();
	ImGui::TextDisabled("Auto material");
	if (!d.autoMaterialRequested)
	{
		ImGui::TextDisabled("Auto material off (using manual metallic/roughness/emissive)");
	}
	else if (d.materialAutoOk == 1)
	{
		ImGui::TextColored(kOk, ICON_FA_CIRCLE_CHECK " Detected: metallic %.2f, roughness %.2f, emissive %.2f",
			d.materialMetallic, d.materialRoughness, d.materialEmissive);
	}
	else if (d.materialAutoOk == 0)
	{
		// Auto detect requested but the game function didn't return usable patterns.
		ImGui::TextColored(kWarn, ICON_FA_TRIANGLE_EXCLAMATION " Auto detect fell back (%s)",
			d.materialSource.empty() ? "source samples" : d.materialSource.c_str());
		if (!d.materialFailure.empty() && d.materialFailure != "ok")
			ImGui::TextColored(kWarn, "  reason: %s", d.materialFailure.c_str());
		if (d.materialSource == "fill_material_only")
			ImGui::TextDisabled("  no Paint region — set a region to Paint for auto material");
	}
	else
	{
		ImGui::TextDisabled("Auto material on — run a paint to collect status");
	}

	if (d.lastJobOk)
		ImGui::TextColored(kOk, ICON_FA_CIRCLE_CHECK " Last %s OK", d.lastKind.c_str());
	else if (!d.lastFailure.empty() && d.lastFailure.find("mesh_paint_cancelled") != std::string::npos)
		ImGui::TextColored(kWarn, ICON_FA_TRIANGLE_EXCLAMATION " Last %s stopped (partial apply)", d.lastKind.c_str());
	else
		ImGui::TextColored(kBad, ICON_FA_CIRCLE_XMARK " Last %s failed", d.lastKind.c_str());

	const unsigned long long nowMs = GetTickCount64();
	const double ageSec = nowMs >= d.updatedMs ? (nowMs - d.updatedMs) / 1000.0 : 0.0;
	ImGui::TextDisabled("Updated %.1fs ago", ageSec);
}
