#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct CamoSettings
{
	// Default paint profile: both brushes, auto material, all regions paint, hotkeys on.
	float brush1Texels = 25.0f;
	float brush2Texels = 5.0f;
	bool brush1Enabled = true;
	bool brush2Enabled = true;
	bool batchAutoAdapt = true;
	int batchLimit = 20;
	int batchPacingMs = 50;
	float sideSourceMaxUv = 0.08f;
	float frontBackSourceMaxUv = 0.45f;
	int frontRegionMode = 0; // 0=paint, 1=fill, 2=skip
	int sideRegionMode = 0;
	int backRegionMode = 0;
	bool autoMaterial = true;
	float metallic = 0.0f;
	float roughness = 1.0f;
	float emissive = 0.0f;
	char fillColorHex[8] = "#FFFFFF";
	float fillMetallic = 1.0f;
	float fillRoughness = 0.0f;
	float fillEmissive = 0.0f;
	int startHotkey = 0x70;     // VK_F1
	int previewHotkey = 0x71;   // VK_F2
	int unpreviewHotkey = 0x72; // VK_F3
	int stopHotkey = 0x73;      // VK_F4
	bool hotkeysEnabled = true;
	bool showDiagnostics = false;

	bool UsesFill() const
	{
		return frontRegionMode == 1 || sideRegionMode == 1 || backRegionMode == 1;
	}

	// Smallest enabled brush size drives UV coverage step.
	float CoverageStepTexels() const;

	void ApplyDefaults();
	void ClampLimits();
	void Load();
	void Save() const;
	bool LoadFromPath(const char* path);
	bool SaveToPath(const char* path) const;

	// Named presets under C:\peterhack\camo_presets\<name>.cfg (same INI format as camo.cfg).
	bool SavePreset(const std::string& name) const;
	bool LoadPreset(const std::string& name);
	static bool DeletePreset(const std::string& name);
	static std::vector<std::string> ListPresets();

	static const char* RegionModeName(int mode);
};
