#include "includes.hpp"

#pragma warning(disable : 4244)
#pragma warning(disable : 4305)

namespace
{
	ImDrawList* ResolveDrawList(ImDrawList* dl)
	{
		if (dl)
			return dl;
		if (ImDrawList* windowList = ImGui::GetWindowDrawList())
			return windowList;
		return ImGui::GetBackgroundDrawList();
	}

	float OutlineBorder(float thickness)
	{
		return thickness < 1.0f ? 1.0f : thickness;
	}

	// Fill a solid outline by drawing text on concentric rings. A single 8-way
	// pass at the full radius leaves separated ghost copies when thickness > 1.
	void DrawTextOutline(ImDrawList* dl, const ImVec2& pos, ImU32 outlineColor, const char* text,
		float outlineThickness)
	{
		const int rings = static_cast<int>(outlineThickness + 0.999f);
		if (rings <= 0)
			return;

		static constexpr ImVec2 kDirs[] = {
			{ -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f },
			{ -0.70710678f, -0.70710678f }, { 0.70710678f, -0.70710678f },
			{ -0.70710678f, 0.70710678f }, { 0.70710678f, 0.70710678f },
		};

		for (int ring = 1; ring <= rings; ++ring)
		{
			const float radius = static_cast<float>(ring);
			for (const ImVec2& dir : kDirs)
				dl->AddText(ImVec2(pos.x + dir.x * radius, pos.y + dir.y * radius), outlineColor, text);
		}
	}
}

void Drawings::DrawLine(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 color, float thickness,
	bool outline, ImU32 outlineColor, float outlineThickness)
{
	dl = ResolveDrawList(dl);
	if (!dl)
		return;

	const float border = outline ? OutlineBorder(outlineThickness) : 0.0f;
	if (outline)
		dl->AddLine(a, b, outlineColor, thickness + border * 2.0f);
	dl->AddLine(a, b, color, thickness);
}

void Drawings::DrawText(ImDrawList* dl, const ImVec2& pos, ImU32 color, const char* text,
	bool outline, ImU32 outlineColor, float outlineThickness)
{
	dl = ResolveDrawList(dl);
	if (!dl || !text)
		return;

	if (outline)
		DrawTextOutline(dl, pos, outlineColor, text, outlineThickness);
	dl->AddText(pos, color, text);
}

void Drawings::DrawBox(ImDrawList* dl, int X, int Y, int W, int H, const ImU32& color, int thickness,
	bool outline, ImU32 outlineColor, float outlineThickness)
{
	dl = ResolveDrawList(dl);
	if (!dl)
		return;

	const float lineW = (W / 1.0f);
	const float lineH = (H / 1.0f);
	const float fx = static_cast<float>(X);
	const float fy = static_cast<float>(Y);
	const float fw = static_cast<float>(W);
	const float fh = static_cast<float>(H);
	const float ft = static_cast<float>(thickness);
	const float outlineLineThickness = ft + OutlineBorder(outlineThickness) * 2.0f;

	auto drawCorner = [&](ImU32 lineColor, float lineThickness)
	{
		dl->AddLine(ImVec2(fx, fy), ImVec2(fx, fy + lineH), lineColor, lineThickness);
		dl->AddLine(ImVec2(fx, fy), ImVec2(fx + lineW, fy), lineColor, lineThickness);
		dl->AddLine(ImVec2(fx + fw - lineW, fy), ImVec2(fx + fw, fy), lineColor, lineThickness);
		dl->AddLine(ImVec2(fx + fw, fy), ImVec2(fx + fw, fy + lineH), lineColor, lineThickness);
		dl->AddLine(ImVec2(fx, fy + fh - lineH), ImVec2(fx, fy + fh), lineColor, lineThickness);
		dl->AddLine(ImVec2(fx, fy + fh), ImVec2(fx + lineW, fy + fh), lineColor, lineThickness);
		dl->AddLine(ImVec2(fx + fw - lineW, fy + fh), ImVec2(fx + fw, fy + fh), lineColor, lineThickness);
		dl->AddLine(ImVec2(fx + fw, fy + fh - lineH), ImVec2(fx + fw, fy + fh), lineColor, lineThickness);
	};

	if (outline)
		drawCorner(outlineColor, outlineLineThickness);
	drawCorner(color, ft);
}
