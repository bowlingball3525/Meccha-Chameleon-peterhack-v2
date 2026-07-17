#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct CamoSettings
{
	float brush1Texels = 30.0f;
	float brush2Texels = 10.0f;
	int batchLimit = 20;
	int batchPacingMs = 50;
	float sideSourceMaxUv = 0.08f;
	float frontBackSourceMaxUv = 0.45f;
	int frontRegionMode = 0; // 0=paint, 1=fill, 2=skip
	int sideRegionMode = 0;
	int backRegionMode = 0;
	bool autoMaterial = true;
	float metallic = 0.0f;
	float roughness = 0.65f;
	char fillColorHex[8] = "#FFFFFF";
	float fillMetallic = 0.0f;
	float fillRoughness = 0.65f;
	int startHotkey = 0x70;     // VK_F1
	int previewHotkey = 0x71;   // VK_F2
	int unpreviewHotkey = 0x72; // VK_F3
	int stopHotkey = 0x73;      // VK_F4
	bool hotkeysEnabled = false;
	bool showDiagnostics = false;

	bool UsesFill() const
	{
		return frontRegionMode == 1 || sideRegionMode == 1 || backRegionMode == 1;
	}

	void ApplyDefaults();
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
