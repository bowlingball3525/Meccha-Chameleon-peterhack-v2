#pragma once

#include <cstdint>

struct CamoSettings
{
	float brush1Texels = 30.0f;
	float brush2Texels = 10.0f;
	int batchLimit = 20;
	int batchPacingMs = 50;
	float sideSourceMaxUv = 0.08f;
	float frontBackSourceMaxUv = 0.45f;
	int frontRegionMode = 1; // 0=paint, 1=fill, 2=skip
	int sideRegionMode = 0;
	int backRegionMode = 0;
	bool autoMaterial = false;
	float metallic = 0.0f;
	float roughness = 1.0f;
	char fillColorHex[8] = "#FFFFFF";
	float fillMetallic = 1.0f;
	float fillRoughness = 0.0f;
	int startHotkey = 0x79;     // VK_F10 — F1 is often captured by the game/OS
	int previewHotkey = 0x71;   // VK_F2
	int unpreviewHotkey = 0x72; // VK_F3
	int stopHotkey = 0x73;      // VK_F4
	bool hotkeysEnabled = false;

	void ApplyDefaults();
	void Load();
	void Save() const;

	static const char* RegionModeName(int mode);
};
