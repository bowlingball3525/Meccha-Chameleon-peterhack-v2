#pragma once

#include <string>
#include "imgui/imgui.h"

// Lightweight, thread-safe toast/notification feed. Producers (any thread) call
// Add/Info/Success/Warn/Error; the render thread calls Draw() once per frame.
namespace Notify
{
	void Add(const std::string& text, ImU32 color = IM_COL32(230, 230, 230, 255), unsigned int durationMs = 3200);
	void Info(const std::string& text);
	void Success(const std::string& text);
	void Warn(const std::string& text);
	void Error(const std::string& text);

	// Draw + expire active toasts (top-right). Call from the render thread.
	void Draw();
}
