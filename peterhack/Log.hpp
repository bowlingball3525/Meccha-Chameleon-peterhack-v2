#pragma once

#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <iostream>

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
}

inline void PhLog(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stdout, fmt, args);
	va_end(args);
}
