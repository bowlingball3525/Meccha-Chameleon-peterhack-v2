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

}

float CamoSettings::CoverageStepTexels() const
{
	if (brush1Enabled && brush2Enabled)
		return brush1Texels < brush2Texels ? brush1Texels : brush2Texels;
	if (brush1Enabled)
		return brush1Texels;
	return brush2Texels;
}

void CamoSettings::ClampLimits()
{
	if (brush1Texels < 10.0f)
		brush1Texels = 10.0f;
	if (brush1Texels > 50.0f)
		brush1Texels = 50.0f;
	if (brush2Texels < 1.0f)
		brush2Texels = 1.0f;
	if (brush2Texels > 10.0f)
		brush2Texels = 10.0f;
	if (batchLimit < 1)
		batchLimit = 1;
	if (batchLimit > 500)
		batchLimit = 500;
	if (batchPacingMs < 1)
		batchPacingMs = 1;
	if (batchPacingMs > 500)
		batchPacingMs = 500;
	if (sideSourceMaxUv < 0.001f)
		sideSourceMaxUv = 0.001f;
	if (sideSourceMaxUv > 0.50f)
		sideSourceMaxUv = 0.50f;
	if (frontBackSourceMaxUv < 0.001f)
		frontBackSourceMaxUv = 0.001f;
	if (frontBackSourceMaxUv > 2.0f)
		frontBackSourceMaxUv = 2.0f;
	if (metallic < 0.0f)
		metallic = 0.0f;
	if (metallic > 1.0f)
		metallic = 1.0f;
	if (roughness < 0.0f)
		roughness = 0.0f;
	if (roughness > 1.0f)
		roughness = 1.0f;
	if (emissive < 0.0f)
		emissive = 0.0f;
	if (emissive > 1.0f)
		emissive = 1.0f;
	if (fillMetallic < 0.0f)
		fillMetallic = 0.0f;
	if (fillMetallic > 1.0f)
		fillMetallic = 1.0f;
	if (fillRoughness < 0.0f)
		fillRoughness = 0.0f;
	if (fillRoughness > 1.0f)
		fillRoughness = 1.0f;
	if (fillEmissive < 0.0f)
		fillEmissive = 0.0f;
	if (fillEmissive > 1.0f)
		fillEmissive = 1.0f;
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
	*this = CamoSettings{};
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
		if (_stricmp(key, "brush1") == 0 || _stricmp(key, "brush_1_size_texels") == 0)
			ParseFloat(value, brush1Texels);
		else if (_stricmp(key, "brush2") == 0 || _stricmp(key, "brush_2_size_texels") == 0)
			ParseFloat(value, brush2Texels);
		else if (_stricmp(key, "brush_1_enabled") == 0)
			brush1Enabled = (_stricmp(value, "1") == 0 || _stricmp(value, "true") == 0);
		else if (_stricmp(key, "brush_2_enabled") == 0)
			brush2Enabled = (_stricmp(value, "1") == 0 || _stricmp(value, "true") == 0);
		else if (_stricmp(key, "batch_auto_adapt") == 0)
			batchAutoAdapt = (_stricmp(value, "1") == 0 || _stricmp(value, "true") == 0);
		else if (_stricmp(key, "batch") == 0)
			ParseInt(value, batchLimit);
		else if (_stricmp(key, "packed_batch_limit") == 0)
			ParseInt(value, batchLimit);
		else if (_stricmp(key, "pacing_ms") == 0)
			ParseInt(value, batchPacingMs);
		else if (_stricmp(key, "packed_batch_pacing_ms") == 0)
			ParseInt(value, batchPacingMs);
		else if (_stricmp(key, "side_uv") == 0)
			ParseFloat(value, sideSourceMaxUv);
		else if (_stricmp(key, "front_back_uv") == 0)
			ParseFloat(value, frontBackSourceMaxUv);
		else if (_stricmp(key, "front_mode") == 0)
			ParseRegion(value, frontRegionMode);
		else if (_stricmp(key, "front_region_mode") == 0)
			ParseRegion(value, frontRegionMode);
		else if (_stricmp(key, "side_mode") == 0)
			ParseRegion(value, sideRegionMode);
		else if (_stricmp(key, "side_region_mode") == 0)
			ParseRegion(value, sideRegionMode);
		else if (_stricmp(key, "back_mode") == 0)
			ParseRegion(value, backRegionMode);
		else if (_stricmp(key, "back_region_mode") == 0)
			ParseRegion(value, backRegionMode);
		else if (_stricmp(key, "auto_material") == 0)
			autoMaterial = (_stricmp(value, "1") == 0 || _stricmp(value, "true") == 0);
		else if (_stricmp(key, "metallic") == 0)
			ParseFloat(value, metallic);
		else if (_stricmp(key, "roughness") == 0)
			ParseFloat(value, roughness);
		else if (_stricmp(key, "emissive") == 0)
			ParseFloat(value, emissive);
		else if (_stricmp(key, "fill_color") == 0)
		{
			if (value[0] == '#')
				strncpy_s(fillColorHex, value, _TRUNCATE);
		}
		else if (_stricmp(key, "fill_metallic") == 0)
			ParseFloat(value, fillMetallic);
		else if (_stricmp(key, "fill_roughness") == 0)
			ParseFloat(value, fillRoughness);
		else if (_stricmp(key, "fill_emissive") == 0)
			ParseFloat(value, fillEmissive);
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

	ClampLimits();
	SanitizeHotkeys(*this);
	return true;
}

bool CamoSettings::SaveToPath(const char* path) const
{
	if (!path || !*path)
		return false;

	CamoSettings normalized = *this;
	normalized.ClampLimits();
	_mkdir("C:\\peterhack");
	FILE* file = nullptr;
	if (fopen_s(&file, path, "w") != 0 || !file)
		return false;

	fprintf(file, "brush1=%.3f\n", normalized.brush1Texels);
	fprintf(file, "brush2=%.3f\n", normalized.brush2Texels);
	fprintf(file, "brush_1_enabled=%d\n", normalized.brush1Enabled ? 1 : 0);
	fprintf(file, "brush_2_enabled=%d\n", normalized.brush2Enabled ? 1 : 0);
	fprintf(file, "batch_auto_adapt=%d\n", normalized.batchAutoAdapt ? 1 : 0);
	fprintf(file, "batch=%d\n", normalized.batchLimit);
	fprintf(file, "pacing_ms=%d\n", normalized.batchPacingMs);
	fprintf(file, "side_uv=%.4f\n", normalized.sideSourceMaxUv);
	fprintf(file, "front_back_uv=%.4f\n", normalized.frontBackSourceMaxUv);
	fprintf(file, "front_mode=%s\n", RegionModeName(normalized.frontRegionMode));
	fprintf(file, "side_mode=%s\n", RegionModeName(normalized.sideRegionMode));
	fprintf(file, "back_mode=%s\n", RegionModeName(normalized.backRegionMode));
	fprintf(file, "auto_material=%d\n", normalized.autoMaterial ? 1 : 0);
	fprintf(file, "metallic=%.3f\n", normalized.metallic);
	fprintf(file, "roughness=%.3f\n", normalized.roughness);
	fprintf(file, "emissive=%.3f\n", normalized.emissive);
	fprintf(file, "fill_color=%s\n", normalized.fillColorHex);
	fprintf(file, "fill_metallic=%.3f\n", normalized.fillMetallic);
	fprintf(file, "fill_roughness=%.3f\n", normalized.fillRoughness);
	fprintf(file, "fill_emissive=%.3f\n", normalized.fillEmissive);
	fprintf(file, "hk_start=%d\n", normalized.startHotkey);
	fprintf(file, "hk_preview=%d\n", normalized.previewHotkey);
	fprintf(file, "hk_unpreview=%d\n", normalized.unpreviewHotkey);
	fprintf(file, "hk_stop=%d\n", normalized.stopHotkey);
	fprintf(file, "hotkeys_enabled=%d\n", normalized.hotkeysEnabled ? 1 : 0);
	fprintf(file, "show_diagnostics=%d\n", normalized.showDiagnostics ? 1 : 0);
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
