#include "CamoSettings.hpp"
#include "Keybinds.hpp"
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <direct.h>

namespace
{
	const char* kCamoConfigPath = "C:\\peterhack\\camo.cfg";
	const char* kPresetDir = "C:\\peterhack\\camo_presets";

	void Trim(char* s)
	{
		if (!s)
			return;
		char* start = s;
		while (*start == ' ' || *start == '\t')
			++start;
		if (start != s)
			memmove(s, start, strlen(start) + 1);
		size_t len = strlen(s);
		while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n'))
			s[--len] = '\0';
	}

	bool ParseFloat(const char* value, float& out)
	{
		if (!value || !*value)
			return false;
		out = static_cast<float>(atof(value));
		return true;
	}

	bool ParseInt(const char* value, int& out)
	{
		if (!value || !*value)
			return false;
		out = atoi(value);
		return true;
	}

	bool ParseRegion(const char* value, int& out)
	{
		if (!value)
			return false;
		if (_stricmp(value, "fill") == 0)
		{
			out = 1;
			return true;
		}
		if (_stricmp(value, "skip") == 0)
		{
			out = 2;
			return true;
		}
		out = 0;
		return true;
	}

	std::string SanitizePresetName(const std::string& name)
	{
		std::string clean;
		clean.reserve(name.size());
		for (char c : name)
		{
			if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' ||
				c == '<' || c == '>' || c == '|')
				continue;
			if (c == ' ' || c == '\t')
			{
				if (!clean.empty() && clean.back() != '_')
					clean.push_back('_');
				continue;
			}
			clean.push_back(c);
		}
		while (!clean.empty() && clean.back() == '_')
			clean.pop_back();
		if (clean.empty())
			clean = "preset";
		return clean;
	}

	std::string PresetPath(const std::string& name)
	{
		return std::string(kPresetDir) + "\\" + SanitizePresetName(name) + ".cfg";
	}

	void SanitizeHotkeys(CamoSettings& s)
	{
		auto sanitizeHotkey = [](int& bind) {
			if (Binds::IsPadBind(bind))
			{
				if (!Binds::IsValidBind(bind))
					bind = 0x70;
				return;
			}
			if (bind == 0x01 || bind == 0x02 || bind == 0x04 || bind == 0x05 || bind == 0x06 ||
				bind == 0x2D || bind == 0x79 || !Binds::IsValidBind(bind))
				bind = 0x70;
		};
		sanitizeHotkey(s.startHotkey);
		sanitizeHotkey(s.previewHotkey);
		sanitizeHotkey(s.unpreviewHotkey);
		sanitizeHotkey(s.stopHotkey);

		if (s.startHotkey == 0x79)
			s.startHotkey = 0x70;
		if (s.previewHotkey == 0x79)
			s.previewHotkey = 0x71;
		if (s.unpreviewHotkey == 0x79)
			s.unpreviewHotkey = 0x72;
		if (s.stopHotkey == 0x79)
			s.stopHotkey = 0x73;
	}

	void ClampCamoLimits(CamoSettings& s)
	{
		if (s.batchLimit < 1)
			s.batchLimit = 1;
		if (s.batchLimit > 32)
			s.batchLimit = 32;
		if (s.batchPacingMs < 50)
			s.batchPacingMs = 50;
		if (s.batchPacingMs > 500)
			s.batchPacingMs = 500;
	}
}

const char* CamoSettings::RegionModeName(int mode)
{
	switch (mode)
	{
	case 1: return "fill";
	case 2: return "skip";
	default: return "paint";
	}
}

void CamoSettings::ApplyDefaults()
{
	brush1Texels = 30.0f;
	brush2Texels = 10.0f;
	batchLimit = 20;
	batchPacingMs = 50;
	sideSourceMaxUv = 0.08f;
	frontBackSourceMaxUv = 0.45f;
	frontRegionMode = 1;
	sideRegionMode = 0;
	backRegionMode = 0;
	autoMaterial = false;
	metallic = 0.0f;
	roughness = 1.0f;
	strcpy_s(fillColorHex, "#FFFFFF");
	fillMetallic = 1.0f;
	fillRoughness = 0.0f;
	startHotkey = 0x70; // VK_F1
	previewHotkey = 0x71;
	unpreviewHotkey = 0x72;
	stopHotkey = 0x73;
	hotkeysEnabled = false;
	showDiagnostics = false;
}

bool CamoSettings::LoadFromPath(const char* path)
{
	if (!path || !*path)
		return false;

	ApplyDefaults();
	FILE* file = nullptr;
	if (fopen_s(&file, path, "r") != 0 || !file)
		return false;

	char line[512];
	while (fgets(line, sizeof(line), file))
	{
		char* eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		char* key = line;
		char* value = eq + 1;
		Trim(key);
		Trim(value);
		if (_stricmp(key, "brush1") == 0)
			ParseFloat(value, brush1Texels);
		else if (_stricmp(key, "brush2") == 0)
			ParseFloat(value, brush2Texels);
		else if (_stricmp(key, "batch") == 0)
			ParseInt(value, batchLimit);
		else if (_stricmp(key, "pacing_ms") == 0)
			ParseInt(value, batchPacingMs);
		else if (_stricmp(key, "side_uv") == 0)
			ParseFloat(value, sideSourceMaxUv);
		else if (_stricmp(key, "front_back_uv") == 0)
			ParseFloat(value, frontBackSourceMaxUv);
		else if (_stricmp(key, "front_mode") == 0)
			ParseRegion(value, frontRegionMode);
		else if (_stricmp(key, "side_mode") == 0)
			ParseRegion(value, sideRegionMode);
		else if (_stricmp(key, "back_mode") == 0)
			ParseRegion(value, backRegionMode);
		else if (_stricmp(key, "auto_material") == 0)
			autoMaterial = (_stricmp(value, "1") == 0 || _stricmp(value, "true") == 0);
		else if (_stricmp(key, "metallic") == 0)
			ParseFloat(value, metallic);
		else if (_stricmp(key, "roughness") == 0)
			ParseFloat(value, roughness);
		else if (_stricmp(key, "fill_color") == 0)
		{
			if (value[0] == '#')
				strncpy_s(fillColorHex, value, _TRUNCATE);
		}
		else if (_stricmp(key, "fill_metallic") == 0)
			ParseFloat(value, fillMetallic);
		else if (_stricmp(key, "fill_roughness") == 0)
			ParseFloat(value, fillRoughness);
		else if (_stricmp(key, "hk_start") == 0)
			ParseInt(value, startHotkey);
		else if (_stricmp(key, "hk_preview") == 0)
			ParseInt(value, previewHotkey);
		else if (_stricmp(key, "hk_unpreview") == 0)
			ParseInt(value, unpreviewHotkey);
		else if (_stricmp(key, "hk_stop") == 0)
			ParseInt(value, stopHotkey);
		else if (_stricmp(key, "hotkeys_enabled") == 0)
			hotkeysEnabled = (_stricmp(value, "1") == 0 || _stricmp(value, "true") == 0);
		else if (_stricmp(key, "show_diagnostics") == 0)
			showDiagnostics = (_stricmp(value, "1") == 0 || _stricmp(value, "true") == 0);
	}
	fclose(file);

	ClampCamoLimits(*this);
	SanitizeHotkeys(*this);
	return true;
}

bool CamoSettings::SaveToPath(const char* path) const
{
	if (!path || !*path)
		return false;

	_mkdir("C:\\peterhack");
	FILE* file = nullptr;
	if (fopen_s(&file, path, "w") != 0 || !file)
		return false;

	fprintf(file, "brush1=%.3f\n", brush1Texels);
	fprintf(file, "brush2=%.3f\n", brush2Texels);
	fprintf(file, "batch=%d\n", batchLimit);
	fprintf(file, "pacing_ms=%d\n", batchPacingMs);
	fprintf(file, "side_uv=%.4f\n", sideSourceMaxUv);
	fprintf(file, "front_back_uv=%.4f\n", frontBackSourceMaxUv);
	fprintf(file, "front_mode=%s\n", RegionModeName(frontRegionMode));
	fprintf(file, "side_mode=%s\n", RegionModeName(sideRegionMode));
	fprintf(file, "back_mode=%s\n", RegionModeName(backRegionMode));
	fprintf(file, "auto_material=%d\n", autoMaterial ? 1 : 0);
	fprintf(file, "metallic=%.3f\n", metallic);
	fprintf(file, "roughness=%.3f\n", roughness);
	fprintf(file, "fill_color=%s\n", fillColorHex);
	fprintf(file, "fill_metallic=%.3f\n", fillMetallic);
	fprintf(file, "fill_roughness=%.3f\n", fillRoughness);
	fprintf(file, "hk_start=%d\n", startHotkey);
	fprintf(file, "hk_preview=%d\n", previewHotkey);
	fprintf(file, "hk_unpreview=%d\n", unpreviewHotkey);
	fprintf(file, "hk_stop=%d\n", stopHotkey);
	fprintf(file, "hotkeys_enabled=%d\n", hotkeysEnabled ? 1 : 0);
	fprintf(file, "show_diagnostics=%d\n", showDiagnostics ? 1 : 0);
	fclose(file);
	return true;
}

void CamoSettings::Load()
{
	LoadFromPath(kCamoConfigPath);
}

void CamoSettings::Save() const
{
	SaveToPath(kCamoConfigPath);
}

bool CamoSettings::SavePreset(const std::string& name) const
{
	if (name.empty())
		return false;
	_mkdir("C:\\peterhack");
	_mkdir(kPresetDir);
	return SaveToPath(PresetPath(name).c_str());
}

bool CamoSettings::LoadPreset(const std::string& name)
{
	if (name.empty())
		return false;
	return LoadFromPath(PresetPath(name).c_str());
}

bool CamoSettings::DeletePreset(const std::string& name)
{
	if (name.empty())
		return false;
	return remove(PresetPath(name).c_str()) == 0;
}

std::vector<std::string> CamoSettings::ListPresets()
{
	std::vector<std::string> out;
	WIN32_FIND_DATAA fd{};
	const std::string pattern = std::string(kPresetDir) + "\\*.cfg";
	HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE)
		return out;
	do
	{
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		std::string fn = fd.cFileName;
		const std::string ext = ".cfg";
		if (fn.size() > ext.size() && fn.compare(fn.size() - ext.size(), ext.size(), ext) == 0)
			fn.erase(fn.size() - ext.size());
		out.push_back(fn);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	return out;
}
