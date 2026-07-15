#include "Notify.hpp"
#include "Settings.hpp"

#include <Windows.h>
#include <cstdio>
#include <deque>
#include <mutex>
#include <vector>

extern Settings* cfg;

namespace
{
	struct Toast
	{
		std::string text;
		ImU32 color;
		ULONGLONG createdMs;
		unsigned int durationMs;
	};

	std::mutex g_mutex;
	std::deque<Toast> g_toasts;
	constexpr size_t kMaxToasts = 6;
	constexpr unsigned int kFadeMs = 350;

	ImU32 ScaleAlpha(ImU32 col, float alpha)
	{
		int a = (int)((col >> IM_COL32_A_SHIFT) & 0xFF);
		a = (int)(a * alpha);
		if (a < 0) a = 0;
		if (a > 255) a = 255;
		return (col & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
	}

	ImVec4 ColorToVec4(ImU32 col)
	{
		return ImVec4(
			((col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
			((col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
			((col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
			((col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
	}
}

void Notify::Add(const std::string& text, ImU32 color, unsigned int durationMs)
{
	if (text.empty())
		return;
	std::lock_guard<std::mutex> lk(g_mutex);
	g_toasts.push_back({ text, color, GetTickCount64(), durationMs });
	while (g_toasts.size() > kMaxToasts)
		g_toasts.pop_front();
}

void Notify::Info(const std::string& t) { Add(t, IM_COL32(220, 220, 220, 255)); }
void Notify::Success(const std::string& t) { Add(t, IM_COL32(90, 230, 130, 255)); }
void Notify::Warn(const std::string& t) { Add(t, IM_COL32(255, 190, 60, 255)); }
void Notify::Error(const std::string& t) { Add(t, IM_COL32(255, 95, 95, 255)); }

void Notify::Draw()
{
	if (!cfg || !cfg->bNotifications)
		return;

	const ULONGLONG now = GetTickCount64();
	std::vector<Toast> active;
	{
		std::lock_guard<std::mutex> lk(g_mutex);
		for (auto it = g_toasts.begin(); it != g_toasts.end();)
		{
			if (now - it->createdMs >= it->durationMs)
				it = g_toasts.erase(it);
			else
			{
				active.push_back(*it);
				++it;
			}
		}
	}
	if (active.empty())
		return;

	// Use real ImGui windows (not the foreground draw list) so toasts always
	// stack above the full-screen ##scene overlay and the menu. Windows created
	// later in the frame render on top.
	const ImGuiIO& io = ImGui::GetIO();
	const float pad = 12.0f;
	float y = pad + 34.0f;

	for (size_t i = 0; i < active.size(); ++i)
	{
		const Toast& t = active[i];
		const ULONGLONG age = now - t.createdMs;
		float alpha = 1.0f;
		if (age < kFadeMs)
			alpha = (float)age / (float)kFadeMs;
		else if (age > t.durationMs - kFadeMs)
			alpha = (float)(t.durationMs - age) / (float)kFadeMs;
		if (alpha < 0.0f) alpha = 0.0f;
		if (alpha > 1.0f) alpha = 1.0f;

		const ImVec2 ts = ImGui::CalcTextSize(t.text.c_str());
		const float boxW = ts.x + 22.0f;
		const float boxH = ts.y + 12.0f;
		const float x = io.DisplaySize.x - boxW - pad;

		ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(boxW, boxH), ImGuiCond_Always);

		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 6.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(18.0f / 255.0f, 18.0f / 255.0f, 22.0f / 255.0f, 225.0f / 255.0f * alpha));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(70.0f / 255.0f, 70.0f / 255.0f, 82.0f / 255.0f, alpha));

		char id[32];
		snprintf(id, sizeof(id), "##toast%zu", i);
		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_AlwaysAutoResize;

		if (ImGui::Begin(id, nullptr, flags))
		{
			ImVec4 textCol = ColorToVec4(t.color);
			textCol.w *= alpha;
			ImGui::TextColored(textCol, "%s", t.text.c_str());
		}
		ImGui::End();

		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(4);

		y += boxH + 6.0f;
	}
}
