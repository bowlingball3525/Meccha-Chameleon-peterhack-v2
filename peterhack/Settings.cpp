#include "includes.hpp"

#define ConfigFile ("C:\\peterhack\\settings.ini")

void Settings::InitializeSettings()
{
	this->bMenuOpen = false;
	this->bInitHooks = true;
	this->bFovChanger = false;
	this->fFovValue = 90.0f;
	this->bLines = false;
	this->bNames = false;
	this->bRoles = false;
	this->bBox = false;
	this->bSkeleton = false;
	this->bDistance = false;
	this->bEspOutline = false;
	this->fEspOutlineThickness = 2.0f;
	this->iEspOutlineMask = EspOutlineSection::All;
	this->bHunterAmmo = false;
	this->bDecoys = false;
	this->bDumpBones = false;
	this->bDumpDeath = false;
	this->bEnemyOnly = false;
	this->bForceCharacterVisibility = false;
	this->bNoGunCooldown = false;
	this->bAntiDetection = false;
	this->bMagnetEnabled = false;
	this->bMagnetActive = false;
	this->iMagnetKey = 0x47; // G
	this->bControllerBinds = false;
	this->iControllerMenuButton = Binds::MakePadBind(Gamepad::kDefaultMenuButton);
	this->bPreventKick = false;
	this->bInfiniteBullets = false;
	this->bNoDecoyCooldown = false;
	this->bSetDecoyNum = false;
	this->iDecoyCount = 5;
	float colVisible[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
	float colNotVisible[4] = { 0.706f, 0.392f, 1.0f, 1.0f };
	float colLines[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float colDecoy[4] = { 1.0f, 0.6f, 0.0f, 1.0f };
	float colEspOutline[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	this->bGodmode = false;
	this->bSpeedhack = false;
	this->fSpeedMultiplier = 2.0f;
	this->bFly = false;
	this->bNoclip = false;
	this->fFlySpeed = 1200.0f;
	this->bAimbot = false;
	this->iAimKey = VK_RBUTTON;
	this->fAimFov = 120.0f;
	this->fAimSmooth = 4.0f;
	this->bAimVisibleOnly = true;
	this->bAimDrawFov = true;
	this->iAimBone = 0;
	this->bTriggerbot = false;
	this->iTriggerKey = 0;
	this->bSilentAim = false;
	this->bNoRecoil = false;
	this->bOverrideNameplateStats = false;
	this->iCustomLikes = 0;
	this->iCustomKills = 0;
	memcpy(this->colVisible, colVisible, sizeof(colVisible));
	memcpy(this->colNotVisible, colNotVisible, sizeof(colNotVisible));
	memcpy(this->colLines, colLines, sizeof(colLines));
	memcpy(this->colDecoy, colDecoy, sizeof(colDecoy));
	memcpy(this->colEspOutline, colEspOutline, sizeof(colEspOutline));
	this->bStreamproof = false;
	this->bStatusHud = false;
	this->bNotifications = true;
	this->fHudPosX = 12.0f;
	this->fHudPosY = 12.0f;
	this->iHudMask = HudSection::All;
	this->bAutoLoadProfile = false;
	this->szAutoLoadProfile[0] = '\0';
}

void Settings::SaveSettings()
{
	_mkdir("C:\\peterhack");
	fopen_s(&file, ConfigFile, "wb");
	if (file)
	{
		// bDumpBones is a transient runtime command, not a persisted setting - write it as
		// its inert default so a saved config can't carry a pending bone dump.
		Settings tmp = *this;
		tmp.bMagnetActive = false;
		tmp.bDumpBones = false;
		tmp.bDumpDeath = false;
		fwrite(&tmp, sizeof(tmp), 1, file);
		fclose(file);
	}
}

void Settings::LoadSettings()
{
	fopen_s(&file, ConfigFile, "rb");
	if (file)
	{
		fseek(file, 0, SEEK_END);
		auto size = ftell(file);

		if (size == sizeof(*cfg))
		{
			fseek(file, 0, SEEK_SET);
			fread(cfg, sizeof(*cfg), 1, file);
			fclose(file);

			// Never restore the transient command flags from disk - they are not settings.
			cfg->bMagnetActive = false;
			cfg->bDumpBones = false;
			cfg->bDumpDeath = false;
			cfg->szAutoLoadProfile[sizeof(cfg->szAutoLoadProfile) - 1] = '\0';

			if (!Binds::IsValidBind(cfg->iMagnetKey))
				cfg->iMagnetKey = 0x47; // G
			if (!Binds::IsValidBind(cfg->iControllerMenuButton))
				cfg->iControllerMenuButton = Binds::MakePadBind(Gamepad::kDefaultMenuButton);
			if (!Binds::IsValidBind(cfg->iAimKey))
				cfg->iAimKey = VK_RBUTTON;
			if (cfg->iTriggerKey != 0 && !Binds::IsValidBind(cfg->iTriggerKey))
				cfg->iTriggerKey = 0;
			if (cfg->iCustomLikes < 0)
				cfg->iCustomLikes = 0;
			if (cfg->iCustomKills < 0)
				cfg->iCustomKills = 0;
			if (cfg->fEspOutlineThickness < 1.0f)
				cfg->fEspOutlineThickness = 1.0f;
			if (cfg->fEspOutlineThickness > 6.0f)
				cfg->fEspOutlineThickness = 6.0f;
			if (cfg->iEspOutlineMask < 0)
				cfg->iEspOutlineMask = EspOutlineSection::All;
		}
		else
		{
			fclose(file);
			InitializeSettings();
		}
	}
	else
	{
		InitializeSettings();
	}
}

namespace
{
	constexpr char kProfileDir[] = "C:\\peterhack\\profiles";
	constexpr unsigned int kProfileMagic = 0x31434850; // 'PHC1'
	constexpr unsigned int kProfileVersion = 1;

	struct ProfileHeader
	{
		unsigned int magic;
		unsigned int version;
		unsigned int payloadSize;
	};

	std::string ProfilePath(const std::string& name)
	{
		std::string clean;
		for (char c : name)
		{
			// Keep the filename safe; drop path separators and other risky chars.
			if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
				c == '"' || c == '<' || c == '>' || c == '|')
				continue;
			clean.push_back(c);
		}
		if (clean.empty())
			clean = "profile";
		return std::string(kProfileDir) + "\\" + clean + ".phcfg";
	}
}

bool Settings::SaveProfile(const std::string& name) const
{
	if (name.empty())
		return false;
	_mkdir("C:\\peterhack");
	_mkdir(kProfileDir);

	FILE* f = nullptr;
	if (fopen_s(&f, ProfilePath(name).c_str(), "wb") != 0 || !f)
		return false;

	Settings tmp = *this;
	tmp.bMagnetActive = false; // runtime toggle never persists across save/load
	tmp.bDumpBones = false;
	tmp.bDumpDeath = false;

	ProfileHeader header{ kProfileMagic, kProfileVersion, (unsigned int)sizeof(Settings) };
	fwrite(&header, sizeof(header), 1, f);
	fwrite(&tmp, sizeof(tmp), 1, f);
	fclose(f);
	return true;
}

bool Settings::LoadProfile(const std::string& name)
{
	if (name.empty())
		return false;
	FILE* f = nullptr;
	if (fopen_s(&f, ProfilePath(name).c_str(), "rb") != 0 || !f)
		return false;

	ProfileHeader header{};
	if (fread(&header, sizeof(header), 1, f) != 1 ||
		header.magic != kProfileMagic ||
		header.version != kProfileVersion ||
		header.payloadSize != sizeof(Settings))
	{
		fclose(f);
		return false; // format mismatch — leave current settings untouched
	}

	Settings tmp{};
	if (fread(&tmp, sizeof(tmp), 1, f) != 1)
	{
		fclose(f);
		return false;
	}
	fclose(f);

	// The auto-load choice belongs to the local install, not the profile, so
	// preserve it across the load (otherwise loading a profile would clobber
	// which profile auto-loads next launch).
	const bool savedAutoLoad = bAutoLoadProfile;
	char savedAutoName[sizeof(szAutoLoadProfile)];
	memcpy(savedAutoName, szAutoLoadProfile, sizeof(savedAutoName));

	*this = tmp;

	bAutoLoadProfile = savedAutoLoad;
	memcpy(szAutoLoadProfile, savedAutoName, sizeof(szAutoLoadProfile));
	szAutoLoadProfile[sizeof(szAutoLoadProfile) - 1] = '\0';
	bMagnetActive = false;
	bDumpBones = false;
	bDumpDeath = false;
	if (!Binds::IsValidBind(iMagnetKey))
		iMagnetKey = 0x47;
	if (!Binds::IsValidBind(iControllerMenuButton))
		iControllerMenuButton = Binds::MakePadBind(Gamepad::kDefaultMenuButton);
	if (!Binds::IsValidBind(iAimKey))
		iAimKey = VK_RBUTTON;
	if (iTriggerKey != 0 && !Binds::IsValidBind(iTriggerKey))
		iTriggerKey = 0;
	return true;
}

bool Settings::DeleteProfile(const std::string& name)
{
	if (name.empty())
		return false;
	return remove(ProfilePath(name).c_str()) == 0;
}

std::vector<std::string> Settings::ListProfiles()
{
	std::vector<std::string> out;
	WIN32_FIND_DATAA fd{};
	const std::string pattern = std::string(kProfileDir) + "\\*.phcfg";
	HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE)
		return out;
	do
	{
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		std::string fn = fd.cFileName;
		const std::string ext = ".phcfg";
		if (fn.size() > ext.size() && fn.compare(fn.size() - ext.size(), ext.size(), ext) == 0)
			fn.erase(fn.size() - ext.size());
		out.push_back(fn);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	return out;
}
