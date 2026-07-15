#include <string>
#include <vector>

class Settings
{
public:
	bool bMenuOpen;
	bool bInitHooks;
	bool bFovChanger;
	float fFovValue;
	bool bLines;
	bool bNames;
	bool bRoles;
	bool bBox;
	bool bSkeleton;
	bool bDistance;
	bool bHunterAmmo;
	bool bDecoys;
	bool bDumpBones;
	bool bDumpDeath; // transient: dump each character's death-flags once to C:\death.txt
	bool bEnemyOnly;
	bool bForceCharacterVisibility;
	bool bNoGunCooldown;
	bool bAntiDetection;
	bool bMagnetEnabled;
	int iMagnetKey; // bind code: keyboard/mouse VK or controller button (Binds::)
	bool bControllerBinds;
	int iControllerMenuButton; // bind code for opening the menu (default: pad Back/View)
	bool bPreventKick;
	bool bInfiniteBullets;
	bool bNoDecoyCooldown;
	bool bSetDecoyNum;
	int iDecoyCount;
	bool bGodmode;
	bool bSpeedhack;
	float fSpeedMultiplier;
	bool bFly;
	bool bNoclip;
	float fFlySpeed;

	// Phase 4 — Combat / Aim
	bool bAimbot;
	int iAimKey;          // bind code (keyboard/mouse VK or pad); held to aim
	float fAimFov;        // capture radius around crosshair, in pixels
	float fAimSmooth;     // 1 = instant snap, higher = slower/smoother
	bool bAimVisibleOnly; // only target enemies with line of sight
	bool bAimDrawFov;     // draw the FOV circle
	int iAimBone;         // 0 = head, 1 = chest
	bool bTriggerbot;     // auto-fire when crosshair is on an enemy
	int iTriggerKey;      // optional hold key for triggerbot (0 = always on)
	bool bSilentAim;      // redirect shots to nearest enemy on fire
	bool bNoRecoil;       // zero out camera shake / recoil

	// Nameplate stats above username (EEYAN = likes, ME = kills). Server RPCs
	// push replicated values so other players see the override in-match.
	bool bOverrideNameplateStats;
	int iCustomLikes;     // EEYAN / thumbs-up
	int iCustomKills;     // ME / kill count

	float colVisible[4];
	float colNotVisible[4];
	float colLines[4];
	float colDecoy[4];

	// Quality-of-life overlays (render-thread only).
	bool bStreamproof;    // exclude the overlay window from screen capture
	bool bStatusHud;      // small on-screen list of active features
	bool bNotifications;  // toast feed for events

	// Status HUD placement + which sections it shows. Position persists so the
	// HUD stays where the user dragged it (drag is enabled while the menu is
	// open). iHudMask is a bitfield of HudSection bits.
	float fHudPosX;
	float fHudPosY;
	int iHudMask;

	// Auto-load: when enabled, the named profile is loaded once at startup right
	// after settings.ini. Kept in settings.ini (and preserved across LoadProfile)
	// so the choice is a property of the local install, not carried by profiles.
	bool bAutoLoadProfile;
	char szAutoLoadProfile[64];

	void InitializeSettings();
	void SaveSettings();
	void LoadSettings();

	// Named config profiles, stored under C:\peterhack\profiles\<name>.phcfg.
	// These carry a small versioned header so a format change rejects rather than
	// loads garbage. The main settings.ini is left untouched by these.
	bool SaveProfile(const std::string& name) const;
	bool LoadProfile(const std::string& name);
	static bool DeleteProfile(const std::string& name);
	static std::vector<std::string> ListProfiles();
};