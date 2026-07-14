#include "includes.hpp"
#include "Theme.hpp"

void Theme::ApplyDeepDark()
{
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;

	// --- Shape: rounded, tight, thin borders for a modern sleek look ---
	style.WindowPadding     = ImVec2(10.0f, 10.0f);
	style.FramePadding      = ImVec2(8.0f, 4.0f);
	style.ItemSpacing       = ImVec2(8.0f, 6.0f);
	style.ItemInnerSpacing  = ImVec2(6.0f, 6.0f);
	style.ScrollbarSize     = 12.0f;
	style.GrabMinSize       = 10.0f;

	style.WindowBorderSize  = 1.0f;
	style.ChildBorderSize   = 1.0f;
	style.PopupBorderSize   = 1.0f;
	style.FrameBorderSize   = 0.0f;
	style.TabBorderSize     = 0.0f;

	style.WindowRounding    = 8.0f;
	style.ChildRounding     = 6.0f;
	style.FrameRounding     = 5.0f;
	style.PopupRounding     = 6.0f;
	style.ScrollbarRounding = 9.0f;
	style.GrabRounding      = 5.0f;
	style.TabRounding       = 5.0f;

	style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
	style.WindowMenuButtonPosition = ImGuiDir_None;

	// --- Colors: deep dark with a cool blue accent ---
	const ImVec4 accent      = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	const ImVec4 accentDim   = ImVec4(0.26f, 0.59f, 0.98f, 0.55f);
	const ImVec4 accentHover = ImVec4(0.33f, 0.66f, 1.00f, 1.00f);

	colors[ImGuiCol_Text]                   = ImVec4(0.90f, 0.91f, 0.94f, 1.00f);
	colors[ImGuiCol_TextDisabled]           = ImVec4(0.45f, 0.47f, 0.52f, 1.00f);
	colors[ImGuiCol_WindowBg]               = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
	colors[ImGuiCol_ChildBg]                = ImVec4(0.09f, 0.09f, 0.11f, 0.00f);
	colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.10f, 0.98f);
	colors[ImGuiCol_Border]                 = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
	colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg]                = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
	colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.19f, 0.19f, 0.23f, 1.00f);
	colors[ImGuiCol_FrameBgActive]          = ImVec4(0.23f, 0.23f, 0.28f, 1.00f);
	colors[ImGuiCol_TitleBg]                = ImVec4(0.05f, 0.05f, 0.07f, 1.00f);
	colors[ImGuiCol_TitleBgActive]          = ImVec4(0.06f, 0.06f, 0.09f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.05f, 0.05f, 0.07f, 1.00f);
	colors[ImGuiCol_MenuBarBg]              = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
	colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.05f, 0.05f, 0.07f, 0.60f);
	colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.24f, 0.24f, 0.29f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.30f, 0.30f, 0.36f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.36f, 0.36f, 0.43f, 1.00f);
	colors[ImGuiCol_CheckMark]              = accent;
	colors[ImGuiCol_SliderGrab]             = accent;
	colors[ImGuiCol_SliderGrabActive]       = accentHover;
	colors[ImGuiCol_Button]                 = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
	colors[ImGuiCol_ButtonHovered]          = ImVec4(0.24f, 0.35f, 0.55f, 1.00f);
	colors[ImGuiCol_ButtonActive]           = accent;
	colors[ImGuiCol_Header]                 = accentDim;
	colors[ImGuiCol_HeaderHovered]          = ImVec4(0.26f, 0.59f, 0.98f, 0.75f);
	colors[ImGuiCol_HeaderActive]           = accent;
	colors[ImGuiCol_Separator]              = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
	colors[ImGuiCol_SeparatorHovered]       = accentDim;
	colors[ImGuiCol_SeparatorActive]        = accent;
	colors[ImGuiCol_ResizeGrip]             = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
	colors[ImGuiCol_ResizeGripHovered]      = accentDim;
	colors[ImGuiCol_ResizeGripActive]       = accent;
	colors[ImGuiCol_Tab]                    = ImVec4(0.11f, 0.11f, 0.14f, 1.00f);
	colors[ImGuiCol_TabHovered]             = ImVec4(0.24f, 0.35f, 0.55f, 1.00f);
	colors[ImGuiCol_TabActive]              = ImVec4(0.18f, 0.26f, 0.42f, 1.00f);
	colors[ImGuiCol_TabUnfocused]           = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
	colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.13f, 0.16f, 0.24f, 1.00f);
	colors[ImGuiCol_PlotLines]              = accent;
	colors[ImGuiCol_PlotLinesHovered]       = accentHover;
	colors[ImGuiCol_PlotHistogram]          = accent;
	colors[ImGuiCol_PlotHistogramHovered]   = accentHover;
	colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
	colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
	colors[ImGuiCol_TableBorderLight]       = ImVec4(0.13f, 0.13f, 0.16f, 1.00f);
	colors[ImGuiCol_TextSelectedBg]         = accentDim;
	colors[ImGuiCol_DragDropTarget]         = accentHover;
	colors[ImGuiCol_NavHighlight]           = accent;
	colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.55f);
}
