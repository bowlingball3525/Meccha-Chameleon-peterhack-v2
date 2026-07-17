// Status-HUD sections the user can toggle via the dropdown in Misc → Overlays.
// Stored as a bitmask in Settings::iHudMask.
// ESP outline targets — bitmask in Settings::iEspOutlineMask.
namespace EspOutlineSection
{
	enum Bits : int
	{
		Box       = 1 << 0,
		Lines     = 1 << 1,
		Name      = 1 << 2,
		Role      = 1 << 3,
		Distance  = 1 << 4,
		Skeleton  = 1 << 5,
		All       = Box | Lines | Name | Role | Distance | Skeleton,
	};
}

namespace HudSection
{
	enum Bits : int
	{
		Esp       = 1 << 0,
		Aim       = 1 << 1, // FOV changer
		Magnet    = 1 << 2,
		Survivor  = 1 << 3, // decoys, cooldowns, anti-detection
		Hunter    = 1 << 4, // infinite bullets, no gun cooldown
		Movement  = 1 << 5, // godmode, speed, fly, noclip
		Combat    = 1 << 6, // aimbot, triggerbot, silent aim, no recoil
		Camo      = 1 << 7,
		Misc      = 1 << 8, // anti-kick, streamproof
		All       = Esp | Aim | Magnet | Survivor | Hunter | Movement | Combat | Camo | Misc,
	};
}

class Menu
{
public:
	void Init();
	void DrawHud(); // render-thread status overlay (active features + binds)
};