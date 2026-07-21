#pragma once

#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <direct.h>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>

inline FILE* g_phLogFile = nullptr;
inline std::mutex g_phLogMutex;

inline void PhLogV(const char* fmt, va_list args)
{
	std::lock_guard<std::mutex> lock(g_phLogMutex);

	SYSTEMTIME st{};
	GetLocalTime(&st);
	fprintf(stdout, "[%02u:%02u:%02u.%03u] ",
	        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	vfprintf(stdout, fmt, args);
	fflush(stdout);

	if (g_phLogFile)
	{
		fprintf(g_phLogFile, "[%02u:%02u:%02u.%03u] ",
		        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
		va_list fileArgs;
		va_copy(fileArgs, args);
		vfprintf(g_phLogFile, fmt, fileArgs);
		va_end(fileArgs);
		fflush(g_phLogFile);
	}
}

inline void PhLog(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	PhLogV(fmt, args);
	va_end(args);
}

inline void PhInitConsole()
{
	static bool done = false;
	if (done)
		return;
	done = true;

	AllocConsole();
	FILE* dummy = nullptr;
	freopen_s(&dummy, "CONOUT$", "w", stdout);
	freopen_s(&dummy, "CONOUT$", "w", stderr);
	freopen_s(&dummy, "CONIN$", "r", stdin);
	std::ios::sync_with_stdio(false);
	std::cin.tie(nullptr);
	setvbuf(stdout, nullptr, _IONBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);

	_mkdir("C:\\peterhack");
	if (fopen_s(&g_phLogFile, "C:\\peterhack\\peterhack.log", "a") == 0 && g_phLogFile)
		setvbuf(g_phLogFile, nullptr, _IONBF, 0);

	SYSTEMTIME st{};
	GetLocalTime(&st);
	PhLog("================================================================\n");
	PhLog("[INIT] peterhack console + log file active (%04u-%02u-%02u %02u:%02u:%02u)\n",
	      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	PhLog("[INIT] log file: C:\\peterhack\\peterhack.log\n");
}

inline void PhLogThrottledMs(const char* key, DWORD intervalMs, const char* fmt, ...)
{
	static std::mutex throttleMutex;
	static std::unordered_map<std::string, ULONGLONG> lastMs;

	const ULONGLONG now = GetTickCount64();
	{
		std::lock_guard<std::mutex> lock(throttleMutex);
		auto& last = lastMs[key ? key : ""];
		if (key && now - last < intervalMs)
			return;
		last = now;
	}

	va_list args;
	va_start(args, fmt);
	PhLogV(fmt, args);
	va_end(args);
}

inline std::string PhJsonTypeField(const std::string& json)
{
	const std::string needle = "\"type\":\"";
	const size_t p = json.find(needle);
	if (p == std::string::npos)
		return "?";
	const size_t start = p + needle.size();
	const size_t end = json.find('"', start);
	if (end == std::string::npos || end <= start)
		return "?";
	return json.substr(start, end - start);
}

inline bool PhJsonSuccessField(const std::string& json)
{
	return json.find("\"success\":true") != std::string::npos;
}

inline void PhLogJsonLine(const char* tag, const char* direction, const std::string& json, std::size_t maxChars = 8192)
{
	const std::string type = PhJsonTypeField(json);
	if (json.size() <= maxChars)
	{
		PhLog("[%s] %s type=%s (%zu bytes): %s\n", tag, direction, type.c_str(), json.size(), json.c_str());
		return;
	}
	PhLog("[%s] %s type=%s (%zu bytes, truncated): %.8192s...\n",
	      tag, direction, type.c_str(), json.size(), json.c_str());
}

inline bool PhNameLooksInteresting(const std::string& name)
{
	static const char* kKeywords[] = {
		"Paint", "Runtime", "Camo", "Death", "Kick", "Shot", "Trace", "Sync",
		"Material", "Brush", "Decoy", "God", "Hunter", "Survivor", "Packed",
		"Mesh", "Channel", "Emissive", "Metallic", "Roughness", "Bullet",
		"Damage", "Kill", "Spectate", "Ragdoll", "Recoil", "Shake", "Spawn",
	};
	for (const char* keyword : kKeywords)
	{
		if (name.find(keyword) != std::string::npos)
			return true;
	}
	return false;
}
