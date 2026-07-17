class Drawings
{
public:
	void DrawBox(ImDrawList* dl, int X, int Y, int W, int H, const ImU32& color, int thickness,
		bool outline, ImU32 outlineColor, float outlineThickness = 2.0f);
	static void DrawLine(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 color, float thickness,
		bool outline, ImU32 outlineColor, float outlineThickness = 2.0f);
	static void DrawText(ImDrawList* dl, const ImVec2& pos, ImU32 color, const char* text,
		bool outline, ImU32 outlineColor, float outlineThickness = 2.0f);
};
