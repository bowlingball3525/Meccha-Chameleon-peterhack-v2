#include "includes.hpp"
#include "SDK/BP_GameState_LINK_classes.hpp"
#include "OverlayWindow.hpp"
#include "SDK/EnhancedInput_classes.hpp"

namespace SDK::Params
{
	struct BP_FirstPersonCharacter_cLeon_Character_Hunter_C_AntiChatTrace final
	{
		FVector End{};
		ABP_FirstPersonCharacter_Main_C* Target{nullptr};
	};

	struct BP_FirstPersonCharacter_cLeon_Character_Hunter_C_SpawnShotEffect_Server_ final
	{
		FVector Endpoint{};
		bool IsHit{false};
		std::uint8_t Pad_19[0x7]{};
		FRotator HitRotation{};
		std::int32_t Seed{0};
	};
}

#include <cmath>

#if defined _M_X64
typedef uint64_t uintx_t;
#elif defined _M_IX86
typedef uint32_t uintx_t;
#endif

typedef HRESULT(APIENTRY* Present12)(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags);
Present12 oPresent = NULL;

typedef void(APIENTRY* ExecuteCommandLists)(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists);
ExecuteCommandLists oExecuteCommandLists = NULL;

typedef HRESULT(APIENTRY* ResizeBuffers)(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
ResizeBuffers oResizeBuffers = NULL;

typedef void(__fastcall* tProcessEvent)(SDK::UObject*, SDK::UFunction*, void*);
tProcessEvent oProcessEvent = nullptr;

template<typename ClassType>
static SDK::UFunction* SafeGetClassFunction(const char* className, const char* functionName)
{
	__try
	{
		SDK::UClass* cls = ClassType::StaticClass();
		if (!cls)
			return nullptr;
		return cls->GetFunction(className, functionName);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}
}

static bool ProcessEventHookTargetLooksValid(void* target)
{
	if (!target)
		return false;
	__try
	{
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(target);
		// UE ProcessEvent prologue typically starts with stack/reg saves, not int3/zeros.
		if (bytes[0] == 0xCC || bytes[0] == 0x00)
			return false;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

namespace Process
{
	DWORD ID;
	HANDLE Handle;
	HWND Hwnd;
	HMODULE Module;
	WNDPROC WndProc;
	int WindowWidth;
	int WindowHeight;
}

static bool IsGameWindowFocused()
{
	const HWND fg = GetForegroundWindow();
	if (!fg)
		return false;
	if (Process::Hwnd && (fg == Process::Hwnd || IsChild(Process::Hwnd, fg)))
		return true;
	DWORD pid = 0;
	GetWindowThreadProcessId(fg, &pid);
	return pid == GetCurrentProcessId();
}

static bool CamoUsesPadHotkeys()
{
	if (!g_camo || !g_camo->settings.hotkeysEnabled)
		return false;
	const auto& s = g_camo->settings;
	return Binds::IsPadBind(s.startHotkey) || Binds::IsPadBind(s.previewHotkey) ||
		Binds::IsPadBind(s.unpreviewHotkey) || Binds::IsPadBind(s.stopHotkey);
}

// Resolve a bundled asset (e.g. the icon font) that ships next to peterhack.dll.
// Probes the module dir, its fonts/ subdir, the Desktop deploy folder, and
// C:\peterhack. Returns a UTF-8 path (ImGui expects UTF-8), empty if not found.
static std::string ResolveAssetPath(const wchar_t* filename)
{
	wchar_t modulePath[MAX_PATH]{};
	HMODULE self = nullptr;
	static int moduleAnchor = 0;
	GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&moduleAnchor), &self);
	if (!self)
		self = GetModuleHandleW(L"peterhack.dll");
	GetModuleFileNameW(self, modulePath, MAX_PATH);

	std::wstring dir(modulePath);
	const size_t slash = dir.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
		dir.resize(slash);

	std::wstring userProfile;
	{
		wchar_t up[MAX_PATH]{};
		const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
		if (n > 0 && n < MAX_PATH)
			userProfile.assign(up, n);
	}

	const std::wstring fname(filename);
	const std::wstring candidates[] = {
		dir + L"\\fonts\\" + fname,
		dir + L"\\" + fname,
		userProfile + L"\\Desktop\\peterhack\\fonts\\" + fname,
		L"C:\\peterhack\\fonts\\" + fname,
	};
	for (const auto& c : candidates)
	{
		if (GetFileAttributesW(c.c_str()) == INVALID_FILE_ATTRIBUTES)
			continue;
		const int need = WideCharToMultiByte(CP_UTF8, 0, c.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (need <= 0)
			continue;
		std::string utf8(need - 1, '\0');
		WideCharToMultiByte(CP_UTF8, 0, c.c_str(), -1, utf8.data(), need, nullptr, nullptr);
		return utf8;
	}
	return {};
}

static void SyncMenuMousePosition(ImGuiIO& io)
{
	if (!Process::Hwnd)
		return;

	POINT pt{};
	if (::GetCursorPos(&pt) && ::ScreenToClient(Process::Hwnd, &pt))
		io.AddMousePosEvent(static_cast<float>(pt.x), static_cast<float>(pt.y));
}

static void ReleaseLocalMouseCapture()
{
	if (!Process::Hwnd)
		return;

	ClipCursor(nullptr);
	if (::GetCapture() == Process::Hwnd)
		::ReleaseCapture();
}

static void RestoreSystemMouseCursor()
{
	ReleaseLocalMouseCapture();

	for (int i = 0; i < 8; ++i)
	{
		if (ShowCursor(TRUE) >= 0)
			break;
	}
	SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

namespace DirectX12Interface
{
	ID3D12Device* Device = nullptr;
	ID3D12DescriptorHeap* DescriptorHeapBackBuffers;
	ID3D12DescriptorHeap* DescriptorHeapImGuiRender;
	ID3D12GraphicsCommandList* CommandList;
	ID3D12CommandQueue* CommandQueue;

	struct _FrameContext
	{
		ID3D12CommandAllocator* CommandAllocator;
		ID3D12Resource* Resource;
		D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHandle;
		UINT64 FenceValue; // GPU fence value of the last submission that used this context's allocator
	};

	uintx_t BuffersCounts = -1;
	_FrameContext* FrameContext;

	// Single fence shared across frame contexts; each context records the value it was last
	// signaled with so we can wait for the GPU to finish before reusing its allocator.
	ID3D12Fence* Fence = nullptr;
	HANDLE FenceEvent = nullptr;
	UINT64 FenceLastSignaledValue = 0;

	// ImGui 1.92+ uploads font + widget textures dynamically and needs many SRV slots.
	// The legacy single-descriptor init corrupts color pickers/gradients on some GPUs,
	// especially when large merged font atlases are built (CJK fonts present).
	static constexpr UINT ImGuiSrvCapacity = 512;
	UINT ImGuiSrvDescriptorSize = 0;
	UINT ImGuiSrvAllocCount = 0;
	std::vector<UINT> ImGuiSrvFreeIndices;
	D3D12_CPU_DESCRIPTOR_HANDLE ImGuiSrvCpuStart{};
	D3D12_GPU_DESCRIPTOR_HANDLE ImGuiSrvGpuStart{};

	void ResetImGuiSrvAllocator()
	{
		ImGuiSrvAllocCount = 0;
		ImGuiSrvFreeIndices.clear();
	}

	void ImGuiSrvDescriptorAlloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu)
	{
		UINT index = UINT_MAX;
		if (!ImGuiSrvFreeIndices.empty())
		{
			index = ImGuiSrvFreeIndices.back();
			ImGuiSrvFreeIndices.pop_back();
		}
		else if (ImGuiSrvAllocCount < ImGuiSrvCapacity)
		{
			index = ImGuiSrvAllocCount++;
		}

		if (index == UINT_MAX)
		{
			*out_cpu = {};
			*out_gpu = {};
			PhLog("[ImGui] SRV descriptor heap exhausted\n");
			return;
		}

		out_cpu->ptr = ImGuiSrvCpuStart.ptr + static_cast<SIZE_T>(index) * ImGuiSrvDescriptorSize;
		out_gpu->ptr = ImGuiSrvGpuStart.ptr + static_cast<UINT64>(index) * ImGuiSrvDescriptorSize;
	}

	void ImGuiSrvDescriptorFree(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE)
	{
		if (!ImGuiSrvDescriptorSize || !ImGuiSrvCpuStart.ptr || !cpu.ptr)
			return;
		const UINT index = static_cast<UINT>((cpu.ptr - ImGuiSrvCpuStart.ptr) / ImGuiSrvDescriptorSize);
		if (index < ImGuiSrvCapacity)
			ImGuiSrvFreeIndices.push_back(index);
	}
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	const bool hasImGui = ImGui::GetCurrentContext() && ImGui::GetIO().BackendPlatformUserData;
	const bool menuOpen = cfg && cfg->bMenuOpen;
	const bool imguiConsumed = hasImGui && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);

	// While the menu is open, keep mouse input in ImGui only — don't pass movement,
	// clicks, or raw input through to the game (camera look, etc.).
	if (menuOpen && hasImGui)
	{
		switch (uMsg)
		{
		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT)
			{
				::SetCursor(nullptr);
				return TRUE;
			}
			break;
		case WM_MOUSEMOVE:
		case WM_NCMOUSEMOVE:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		case WM_MOUSEWHEEL:
		case WM_MOUSEHWHEEL:
		case WM_INPUT:
			return 0;
		}
	}

	if (imguiConsumed)
		return true;

	return CallWindowProc(Process::WndProc, hWnd, uMsg, wParam, lParam);
}

// The world scan (CheatManager::Init) - which both reads the actor list and applies our game-state
// mutations inline - must run ONLY on the engine's game thread.
// Using it let the scan race the real game thread's actor list and fault deep inside
// GetAllActorsOfClass. We positively identify the game thread by the name UE gives it ("GameThread",
// set during FEngineLoop::PreInit) and only run that work when the current TID matches - resolved
// once in hkPresent via FindGameThreadId, then checked in hkProcessEvent.

// Find the engine's game thread by its UE-assigned name ("GameThread"). This is a positive lookup -
// no racing to latch whichever non-render thread happens to dispatch the first ProcessEvent - so it
// can't mis-identify a worker thread as the game thread. The game thread is created once at engine
// boot and lives for the whole process, so the TID we return here stays valid for the session.
// Returns 0 if it can't be found yet (not named, or GetThreadDescription unavailable); the caller
// retries on a later frame.
static DWORD FindGameThreadId()
{
	// GetThreadDescription is Win10 1607+. Resolve it dynamically so we link on any toolset/target.
	typedef HRESULT(WINAPI* PFN_GetThreadDescription)(HANDLE, PWSTR*);
	static PFN_GetThreadDescription pGetThreadDescription =
		reinterpret_cast<PFN_GetThreadDescription>(
			GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
	if (!pGetThreadDescription)
		return 0;

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return 0;

	const DWORD pid = GetCurrentProcessId();
	DWORD foundTid = 0;

	THREADENTRY32 te{};
	te.dwSize = sizeof(te);
	if (Thread32First(snapshot, &te))
	{
		do
		{
			// Snapshot covers every process; keep only our own threads.
			if (te.th32OwnerProcessID != pid)
				continue;

			HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
			if (!thread)
				continue;

			PWSTR desc = nullptr;
			if (SUCCEEDED(pGetThreadDescription(thread, &desc)) && desc)
			{
				if (wcscmp(desc, L"GameThread") == 0)
					foundTid = te.th32ThreadID;
				LocalFree(desc);
			}
			CloseHandle(thread);
		} while (foundTid == 0 && Thread32Next(snapshot, &te));
	}

	CloseHandle(snapshot);
	return foundTid;
}

static void InitKickFunctionPointers()
{
	if (!g_fnKickLINK)
		g_fnKickLINK = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_LINK_C>(
			"BP_FirstPersonCharacter_LINK_C", "Kick");

	if (!g_fnKickOnline)
		g_fnKickOnline = SafeGetClassFunction<SDK::ABP_FirstPersonPlayerState_Online_C>(
			"BP_FirstPersonPlayerState_Online_C", "Kick");

	if (!g_fnClientWasKicked)
		g_fnClientWasKicked = SafeGetClassFunction<SDK::APlayerController>(
			"PlayerController", "ClientWasKicked");

	if (!g_fnClientReturnToMainMenuWithTextReason)
		g_fnClientReturnToMainMenuWithTextReason = SafeGetClassFunction<SDK::APlayerController>(
			"PlayerController", "ClientReturnToMainMenuWithTextReason");

	if (!g_fnOnRepBodyVisibility)
		g_fnOnRepBodyVisibility = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_Main_C", "OnRep_BodyVisibility");

	if (!g_fnDeathPlayer)
		g_fnDeathPlayer = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_Main_C", "DeathPlayer");
	if (!g_fnRagdoll)
		g_fnRagdoll = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_Main_C", "Ragdoll");
	if (!g_fnGoToSpectate)
		g_fnGoToSpectate = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_Main_C", "GoToSpectate");
	if (!g_fnShowDeathWidget)
		g_fnShowDeathWidget = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_Main_C", "ShowDeathWidget");
	if (!g_fnDeathEvent)
		g_fnDeathEvent = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_Main_C", "DeathEvent");
	if (!g_fnDeathUIShowAndAwait)
		g_fnDeathUIShowAndAwait = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_Main_C", "DeathUIShowAndAwait");
	if (!g_fnSpawnDeathSplash)
		g_fnSpawnDeathSplash = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_Main_C", "SpawnDeathSplash");
	if (!g_fnSetSpectatingState)
		g_fnSetSpectatingState = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_Main_C", "SetSpectatingState");
	if (!g_fnGameStateShowDeathWidget)
		g_fnGameStateShowDeathWidget = SafeGetClassFunction<SDK::ABP_GameState_LINK_C>(
			"BP_GameState_LINK_C", "ShowDeathWidget");
	if (!g_fnKillPlayer)
		g_fnKillPlayer = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_cLeon_Character_Hunter_C", "KillPlayer");
	if (!g_fnClientRestart)
		g_fnClientRestart = SafeGetClassFunction<SDK::APlayerController>(
			"PlayerController", "ClientRestart");
}

void ForceRefreshGodmodeFunctionPointers()
{
	InitKickFunctionPointers();
}

static void EnsureGodmodeFunctionPointers()
{
	if (g_fnDeathPlayer && g_fnRagdoll && g_fnGoToSpectate && g_fnShowDeathWidget &&
		g_fnDeathEvent && g_fnDeathUIShowAndAwait && g_fnSpawnDeathSplash &&
		g_fnSetSpectatingState && g_fnGameStateShowDeathWidget && g_fnKillPlayer &&
		g_fnClientRestart)
	{
		return;
	}
	__try
	{
		ForceRefreshGodmodeFunctionPointers();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		PhLog("[ProcessEvent] ForceRefreshGodmodeFunctionPointers fault\n");
	}
}

static bool IsGodmodeDeathFunction(SDK::UFunction* fn)
{
	return fn && (fn == g_fnDeathPlayer || fn == g_fnRagdoll || fn == g_fnGoToSpectate ||
		fn == g_fnShowDeathWidget || fn == g_fnDeathEvent || fn == g_fnDeathUIShowAndAwait ||
		fn == g_fnSpawnDeathSplash);
}

// Minimal parm layouts for godmode blocks — avoids pulling the huge Main
// parameters header into every translation unit.
struct GodmodeSetSpectatingStateParms
{
	bool State;
};
struct GodmodeShowDeathWidgetParms
{
	SDK::APlayerState* TargetPlayerState;
};
struct GodmodeClientRestartParms
{
	SDK::APawn* NewPawn;
};
struct GodmodeKillPlayerParms
{
	SDK::ABP_FirstPersonCharacter_Main_C* FirstpersonCharacter;
	SDK::ABP_FirstPersonPlayerState_Online_C* SourcePlayerState;
};

void InitCombatFunctionPointers()
{
	if (!g_fnLineTraceSingle)
		g_fnLineTraceSingle = SafeGetClassFunction<SDK::UKismetSystemLibrary>(
			"KismetSystemLibrary", "LineTraceSingle");
	if (!g_fnSphereTraceSingle)
		g_fnSphereTraceSingle = SafeGetClassFunction<SDK::UKismetSystemLibrary>(
			"KismetSystemLibrary", "SphereTraceSingle");
	if (!g_fnAntiChatTrace)
		g_fnAntiChatTrace = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_cLeon_Character_Hunter_C", "AntiChatTrace");
	if (!g_fnSpawnShotEffectServer)
		g_fnSpawnShotEffectServer = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_cLeon_Character_Hunter_C", "SpawnShotEffect(Server)");
	if (!g_fnSpawnShotEffectLocal)
		g_fnSpawnShotEffectLocal = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_cLeon_Character_Hunter_C", "SpawnShotEffect(Local)");
	if (!g_fnSpawnShotEffectClient)
		g_fnSpawnShotEffectClient = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_cLeon_Character_Hunter_C", "SpawnShotEffect(Client)");
	if (!g_fnShakeStart)
		g_fnShakeStart = SafeGetClassFunction<SDK::UBPC_CameraShake_C>("BPC_CameraShake_C", "ShakeStart");
	if (!g_fnItemShakeStart)
		g_fnItemShakeStart = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_Main_C", "ItemShakeStart");
	if (!g_fnHunterInpActShot)
		g_fnHunterInpActShot = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_Main_C", "InpActEvt_IA_Shot_K2Node_EnhancedInputActionEvent_18");
	if (!g_fnHunterInpActShot)
		g_fnHunterInpActShot = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_cLeon_Character_Hunter_C", "InpActEvt_IA_Shot_K2Node_EnhancedInputActionEvent_3");
	if (!g_fnKillPlayer)
		g_fnKillPlayer = SafeGetClassFunction<SDK::ABP_FirstPersonCharacter_Main_C>(
			"BP_FirstPersonCharacter_cLeon_Character_Hunter_C", "KillPlayer");
	if (!g_fnClientRestart)
		g_fnClientRestart = SafeGetClassFunction<SDK::APlayerController>(
			"PlayerController", "ClientRestart");
}

void ForceRefreshCombatFunctionPointers()
{
	InitCombatFunctionPointers();
}

static void RefreshCombatFunctionPointersIfStale()
{
	if (!cfg || (!cfg->bSilentAim && !cfg->bTriggerbot && !cfg->bNoRecoil))
		return;

	static ULONGLONG s_lastRefreshMs = 0;
	const ULONGLONG now = GetTickCount64();
	if (now - s_lastRefreshMs < 30000)
		return;
	s_lastRefreshMs = now;
	__try
	{
		InitCombatFunctionPointers();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		PhLog("[ProcessEvent] InitCombatFunctionPointers fault\n");
	}
}

void ForceRefreshKickFunctionPointers()
{
	InitKickFunctionPointers();
}

static void RefreshKickFunctionPointersIfStale()
{
	if (!cfg || (!cfg->bPreventKick && !cfg->bForceCharacterVisibility && !cfg->bGodmode))
		return;

	static ULONGLONG s_lastRefreshMs = 0;
	const ULONGLONG now = GetTickCount64();
	if (now - s_lastRefreshMs < 30000)
		return;
	s_lastRefreshMs = now;
	__try
	{
		InitKickFunctionPointers();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		PhLog("[ProcessEvent] InitKickFunctionPointers fault\n");
	}
}

// Reject obviously corrupt pointers before forwarding into the engine. 0xFFFF… is a
// common stale/freed sentinel and was the fault address in the UE crash report.
static bool LooksLikeUserPtr(const void* p)
{
	const uintptr_t v = reinterpret_cast<uintptr_t>(p);
	return v >= 0x10000 && v < 0x0000FFFF00000000ull;
}

static constexpr bool kVerboseProcessEventLogging = false;

static void VerboseLogProcessEvent(SDK::UObject* obj, SDK::UFunction* fn)
{
	if (!kVerboseProcessEventLogging)
		return;
	if (!fn || !LooksLikeUserPtr(fn))
		return;
	if (obj && !LooksLikeUserPtr(obj))
		obj = nullptr;

	static std::mutex peMutex;
	static std::unordered_map<uintptr_t, std::string> nameCache;
	static std::unordered_map<uintptr_t, ULONGLONG> lastHotMs;
	static std::unordered_map<uintptr_t, bool> loggedCold;

	const uintptr_t fnKey = reinterpret_cast<uintptr_t>(fn);
	const ULONGLONG now = GetTickCount64();

	std::string fnName;
	std::string objName;
	{
		std::lock_guard<std::mutex> lock(peMutex);
		auto& cached = nameCache[fnKey];
		if (cached.empty())
			cached = fn->GetName();
		fnName = cached;
	}
	if (obj)
		objName = obj->GetName();

	if (fnName.empty())
		return;

	if (PhNameLooksInteresting(fnName))
	{
		std::lock_guard<std::mutex> lock(peMutex);
		auto& last = lastHotMs[fnKey];
		if (now - last < 250)
			return;
		last = now;
		PhLog("[PE] %s.%s obj=%s (%p)\n",
		      objName.empty() ? "?" : objName.c_str(),
		      fnName.c_str(),
		      obj ? "ok" : "null",
		      obj);
		return;
	}

	bool firstCold = false;
	{
		std::lock_guard<std::mutex> lock(peMutex);
		firstCold = loggedCold.emplace(fnKey, true).second;
	}
	if (firstCold)
	{
		PhLog("[PE] (first) %s.%s\n",
		      objName.empty() ? "?" : objName.c_str(),
		      fnName.c_str());
	}
}

static void SafeForwardProcessEvent(SDK::UObject* pObject, SDK::UFunction* pFunction, void* pParms)
{
	if (!oProcessEvent)
		return;
	if (!LooksLikeUserPtr(pFunction))
	{
		PhLog("[ProcessEvent] skip forward — bad pFunction %p\n", pFunction);
		return;
	}
	if (pObject && !LooksLikeUserPtr(pObject))
	{
		PhLog("[ProcessEvent] skip forward — bad pObject %p\n", pObject);
		return;
	}
	__try
	{
		oProcessEvent(pObject, pFunction, pParms);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		PhLog("[ProcessEvent] forward fault pObj=%p fn=%p\n", pObject, pFunction);
	}
}

static bool CombatRedirectLive()
{
	if (!g_combatRedirect.active)
		return false;
	if (GetTickCount64() > g_combatRedirect.expireMs)
	{
		g_combatRedirect.active = false;
		return false;
	}
	if (!g_combatRedirect.target || !CheatManager::IsObjectValid(g_combatRedirect.target))
	{
		g_combatRedirect.active = false;
		return false;
	}
	return cfg && (cfg->bSilentAim || cfg->bTriggerbot);
}

static SDK::FVector ExtendTraceEnd(const SDK::FVector& start, const SDK::FVector& hit, double extraPast = 80.0)
{
	const double dx = hit.X - start.X;
	const double dy = hit.Y - start.Y;
	const double dz = hit.Z - start.Z;
	const double len = sqrt(dx * dx + dy * dy + dz * dz);
	if (len < 1.0)
		return hit;
	const double scale = (len + extraPast) / len;
	return { start.X + dx * scale, start.Y + dy * scale, start.Z + dz * scale };
}

static void PatchHitResultForRedirect(SDK::FHitResult& hit, const SDK::FVector& start, const SDK::FVector& end)
{
	auto* target = g_combatRedirect.target;
	auto* leon = target->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass())
		? static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(target)
		: nullptr;
	SDK::UPrimitiveComponent* meshComp = (leon && leon->Mesh) ? leon->Mesh : nullptr;

	const SDK::FVector& impact = g_combatRedirect.hitLocation;
	const double dx = impact.X - start.X;
	const double dy = impact.Y - start.Y;
	const double dz = impact.Z - start.Z;
	const double dist = sqrt(dx * dx + dy * dy + dz * dz);

	hit = {};
	hit.bBlockingHit = true;
	hit.Time = 1.0f;
	hit.Distance = static_cast<float>(dist);
	hit.Location.X = impact.X;
	hit.Location.Y = impact.Y;
	hit.Location.Z = impact.Z;
	hit.ImpactPoint = hit.Location;
	if (dist > 1.0)
	{
		hit.Normal.X = -dx / dist;
		hit.Normal.Y = -dy / dist;
		hit.Normal.Z = -dz / dist;
		hit.ImpactNormal = hit.Normal;
	}
	hit.TraceStart.X = start.X;
	hit.TraceStart.Y = start.Y;
	hit.TraceStart.Z = start.Z;
	hit.TraceEnd.X = end.X;
	hit.TraceEnd.Y = end.Y;
	hit.TraceEnd.Z = end.Z;
	if (target)
	{
		hit.HitObjectHandle.ReferenceObject.ObjectIndex = target->Index;
		hit.HitObjectHandle.ReferenceObject.ObjectSerialNumber = 0;
	}
	if (meshComp)
	{
		hit.Component.ObjectIndex = meshComp->Index;
		hit.Component.ObjectSerialNumber = 0;
	}
}

static thread_local bool s_combatTracePending = false;
static thread_local bool s_silentShotPending = false;
static thread_local SDK::UObject* s_silentShotObject = nullptr;
static thread_local bool s_killPlayerInvokedThisShot = false;

static bool IsLocalCombatPawn(const void* object)
{
	if (!object)
		return false;
	const void* localPawn = g_localPawn.load(std::memory_order_acquire);
	if (localPawn && object == localPawn)
		return true;
	const void* localBody = g_localCharacterBody.load(std::memory_order_acquire);
	return localBody && object == localBody;
}

static bool InputActionValueActuated(const SDK::FInputActionValue& value, float /*triggeredTime*/)
{
	// Boolean IA_Shot: only treat a press (>= 0.5) as a shot. Release/completed events
	// can still carry TriggeredTime and were re-arming redirect without a real shot.
	const double raw = *reinterpret_cast<const double*>(&value);
	return raw >= 0.5;
}

static SDK::UFunction* ResolveLiveFunction(SDK::UObject* object, const char* className, const char* functionName)
{
	if (!object || !className || !functionName)
		return nullptr;
	__try
	{
		SDK::UClass* cls = object->Class;
		if (!cls)
			return nullptr;
		return cls->GetFunction(className, functionName);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}
}

static bool FunctionPointerMatchesLive(SDK::UObject* object, SDK::UFunction* fn, const char* className,
	const char* functionName, SDK::UFunction** cache)
{
	if (!fn || !cache)
		return false;
	if (*cache && fn == *cache)
		return true;
	SDK::UFunction* live = ResolveLiveFunction(object, className, functionName);
	if (live && fn == live)
	{
		*cache = live;
		return true;
	}
	return false;
}

static bool IsKillPlayerFunction(SDK::UObject* object, SDK::UFunction* fn)
{
	if (!fn)
		return false;
	if (g_fnKillPlayer && fn == g_fnKillPlayer)
		return true;
	if (object && FunctionPointerMatchesLive(object, fn,
			"BP_FirstPersonCharacter_cLeon_Character_Hunter_C", "KillPlayer", &g_fnKillPlayer))
		return true;
	return false;
}

static bool IsHunterShotFunction(SDK::UObject* object, SDK::UFunction* fn)
{
	if (!object || !fn || !IsLocalCombatPawn(object))
		return false;
	if (!object->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass()))
		return false;
	if (g_fnHunterInpActShot && fn == g_fnHunterInpActShot)
		return true;

	static const char* kShotEvents[] = {
		"InpActEvt_IA_Shot_K2Node_EnhancedInputActionEvent_18",
		"InpActEvt_IA_Shot_K2Node_EnhancedInputActionEvent_17",
		"InpActEvt_IA_Shot_K2Node_EnhancedInputActionEvent_3",
		"InpActEvt_IA_Shot_K2Node_EnhancedInputActionEvent_2",
		"InpActEvt_IA_Shot_K2Node_EnhancedInputActionEvent_4",
	};
	for (const char* eventName : kShotEvents)
	{
		if (FunctionPointerMatchesLive(object, fn,
				"BP_FirstPersonCharacter_cLeon_Character_Hunter_C", eventName, &g_fnHunterInpActShot))
			return true;
		if (FunctionPointerMatchesLive(object, fn,
				"BP_FirstPersonCharacter_Main_C", eventName, &g_fnHunterInpActShot))
			return true;
	}
	return false;
}

static bool IsLineTraceFunction(SDK::UFunction* fn)
{
	if (!fn)
		return false;
	return fn == g_fnLineTraceSingle || fn == g_fnSphereTraceSingle;
}

static bool IsAntiChatTraceFunction(SDK::UObject* object, SDK::UFunction* fn)
{
	if (!fn)
		return false;
	if (g_fnAntiChatTrace && fn == g_fnAntiChatTrace)
		return true;
	if (object && FunctionPointerMatchesLive(object, fn,
			"BP_FirstPersonCharacter_cLeon_Character_Hunter_C", "AntiChatTrace", &g_fnAntiChatTrace))
		return true;
	return false;
}

static bool IsSpawnShotEffectFunction(SDK::UFunction* fn)
{
	if (!fn)
		return false;
	return fn == g_fnSpawnShotEffectServer || fn == g_fnSpawnShotEffectLocal || fn == g_fnSpawnShotEffectClient;
}

struct HunterShotParmsLayout
{
	SDK::FInputActionValue ActionValue;
	float ElapsedTime;
	float TriggeredTime;
	const SDK::UInputAction* SourceAction;
};

static bool TryArmSilentAimShot(SDK::UObject* pObject, SDK::UFunction* pFunction, void* pParms)
{
	if (!cfg || !cfg->bSilentAim || !pParms || !g_combatRedirect.silentReady || !g_combatRedirect.silentTarget)
		return false;
	if (!IsHunterShotFunction(pObject, pFunction))
		return false;

	const auto* parms = static_cast<const HunterShotParmsLayout*>(pParms);
	if (!InputActionValueActuated(parms->ActionValue, parms->TriggeredTime))
		return false;

	CheatManager::ArmCombatShotRedirect(g_combatRedirect.silentTarget, g_combatRedirect.silentHit);
	s_killPlayerInvokedThisShot = false;
	s_silentShotPending = true;
	s_silentShotObject = pObject;
	return true;
}

static void CombatPreProcessEvent(SDK::UObject* pObject, SDK::UFunction* pFunction, void* pParms)
{
	if (!pFunction || !pParms)
		return;

	TryArmSilentAimShot(pObject, pFunction, pParms);

	if (CombatRedirectLive() && IsKillPlayerFunction(pObject, pFunction) && pParms &&
		pObject && IsLocalCombatPawn(pObject) &&
		g_combatRedirect.target &&
		g_combatRedirect.target->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass()))
	{
		auto* parms = static_cast<GodmodeKillPlayerParms*>(pParms);
		parms->FirstpersonCharacter =
			static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(g_combatRedirect.target);
		s_killPlayerInvokedThisShot = true;
	}

	if (!CombatRedirectLive())
		return;

	if (IsLineTraceFunction(pFunction))
	{
		auto* parms = static_cast<SDK::Params::KismetSystemLibrary_LineTraceSingle*>(pParms);
		parms->End = ExtendTraceEnd(parms->Start, g_combatRedirect.hitLocation);
		s_combatTracePending = true;
		return;
	}

	if (IsAntiChatTraceFunction(pObject, pFunction))
	{
		auto* parms = static_cast<SDK::Params::BP_FirstPersonCharacter_cLeon_Character_Hunter_C_AntiChatTrace*>(pParms);
		parms->End = g_combatRedirect.hitLocation;
		if (!parms->Target &&
			g_combatRedirect.target->IsA(SDK::ABP_FirstPersonCharacter_Main_C::StaticClass()))
		{
			parms->Target = static_cast<SDK::ABP_FirstPersonCharacter_Main_C*>(g_combatRedirect.target);
		}
		return;
	}

	if (IsSpawnShotEffectFunction(pFunction))
	{
		auto* parms = static_cast<SDK::Params::BP_FirstPersonCharacter_cLeon_Character_Hunter_C_SpawnShotEffect_Server_*>(pParms);
		parms->Endpoint = g_combatRedirect.hitLocation;
		parms->IsHit = true;
	}
}

static void CombatPostProcessEvent(SDK::UObject* pObject, SDK::UFunction* pFunction, void* pParms)
{
	if (s_silentShotPending && pObject == s_silentShotObject && IsHunterShotFunction(pObject, pFunction))
	{
		s_silentShotPending = false;
		s_silentShotObject = nullptr;
		s_combatTracePending = false;

		CheatManager::DisarmCombatShotRedirect();

		// The game's shot handler may already invoke KillPlayer after our trace patch.
		// Calling it again on spam-fire was double-killing and could lock out further hits.
		if (!s_killPlayerInvokedThisShot &&
			cfg && cfg->bSilentAim && g_combatRedirect.silentReady && g_combatRedirect.silentTarget &&
			cheat && CheatManager::IsObjectValid(g_combatRedirect.silentTarget))
		{
			cheat->ExecuteSilentAimKill(static_cast<SDK::APawn*>(pObject), g_combatRedirect.silentTarget);
		}
		s_killPlayerInvokedThisShot = false;
	}

	if (!s_combatTracePending || !pFunction || !pParms)
		return;
	if (!IsLineTraceFunction(pFunction))
		return;

	s_combatTracePending = false;
	if (!CombatRedirectLive())
		return;

	auto* parms = static_cast<SDK::Params::KismetSystemLibrary_LineTraceSingle*>(pParms);
	PatchHitResultForRedirect(parms->OutHit, parms->Start, parms->End);
	parms->ReturnValue = true;
}

// Pointer-only exploit blocking — never call GetName() on the ProcessEvent hot path.
static bool TryBlockExploitProcessEvent(SDK::UObject* pObject, SDK::UFunction* pFunction, void* pParms)
{
	if (!cfg || !pFunction)
		return false;

	// No recoil: drop the camera/item shake handlers before they run.
	if (cfg->bNoRecoil &&
		(pFunction == g_fnShakeStart || pFunction == g_fnItemShakeStart))
	{
		return true;
	}

	if (!cfg->bForceCharacterVisibility && !cfg->bPreventKick && !cfg->bGodmode)
		return false;

	// Godmode: drop the death/spectate RPCs when they target our own pawn. The
	// server-authoritative kill (KillPlayer) manifests on the victim's client as
	// these calls; the Invincible/Health field writes don't stop them, so this
	// pointer-only block is what actually keeps us alive through a shot.
	if (cfg->bGodmode)
	{
		EnsureGodmodeFunctionPointers();

		const void* localBody = g_localCharacterBody.load(std::memory_order_acquire);
		if (!localBody)
			localBody = g_localPawn.load(std::memory_order_acquire);
		if (localBody && pObject == localBody)
		{
			if (IsGodmodeDeathFunction(pFunction))
				return true;

			// Block entering spectate, but allow SetSpectatingState(false) through so
			// our recovery writes can clear a stuck death state.
			if (pFunction == g_fnSetSpectatingState && pParms)
			{
				__try
				{
					const auto* parms = static_cast<const GodmodeSetSpectatingStateParms*>(pParms);
					if (parms->State)
						return true;
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
				}
			}
		}

		// Drop hunter KillPlayer RPCs that target our body (server kill path).
		if (pFunction == g_fnKillPlayer && pParms && localBody)
		{
			__try
			{
				const auto* parms = static_cast<const GodmodeKillPlayerParms*>(pParms);
				if (parms->FirstpersonCharacter == localBody)
					return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}
		}

		// Engine repossess to spectate pawn after death — block while godmode is on.
		if (pFunction == g_fnClientRestart && pParms && localBody)
		{
			__try
			{
				const auto* parms = static_cast<const GodmodeClientRestartParms*>(pParms);
				if (parms->NewPawn != localBody)
					return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}
		}

		if (pFunction == g_fnGameStateShowDeathWidget && pParms)
		{
			const void* localPs = g_localPlayerState.load(std::memory_order_acquire);
			if (localPs)
			{
				__try
				{
					const auto* parms = static_cast<const GodmodeShowDeathWidgetParms*>(pParms);
					if (parms->TargetPlayerState == localPs)
						return true;
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
				}
			}
		}
	}

	if (cfg->bForceCharacterVisibility && pFunction == g_fnOnRepBodyVisibility && pObject)
	{
		// Legacy BodyVisibility hook — no-op on current Main character layout.
		(void)pObject;
	}

	if (cfg->bPreventKick &&
		(pFunction == g_fnKickOnline || pFunction == g_fnKickLINK ||
			pFunction == g_fnClientWasKicked || pFunction == g_fnClientReturnToMainMenuWithTextReason))
	{
		if (pFunction == g_fnClientReturnToMainMenuWithTextReason &&
			g_allowSelfReturnToMenu.load(std::memory_order_acquire))
		{
			return false;
		}

		PhLog("[ProcessEvent] Prevented kick function\n");
		return true;
	}

	return false;
}

static void TryRunGameThreadInit(CheatManager* mgr)
{
	// Refresh cached UFunction* on the game thread during our gather pass — never
	// from the bare ProcessEvent hot path (GetFunction during random engine calls
	// was a likely crash source).
	RefreshKickFunctionPointersIfStale();
	RefreshCombatFunctionPointersIfStale();

	__try
	{
		mgr->Init();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		PhLog("[ProcessEvent] Init fault — skipped frame\n");
	}
}

static void TryApplyMenuInputLock(CheatManager* mgr, bool menuOpen)
{
	__try
	{
		mgr->ApplyMenuInputLock(menuOpen);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		PhLog("[ProcessEvent] menu input lock fault\n");
	}
}

void __fastcall hkProcessEvent(SDK::UObject* pObject, SDK::UFunction* pFunction, void* pParms)
{
	VerboseLogProcessEvent(pObject, pFunction);

	if (g_inGameThreadFlush)
	{
		if (TryBlockExploitProcessEvent(pObject, pFunction, pParms))
			return;
		CombatPreProcessEvent(pObject, pFunction, pParms);
		SafeForwardProcessEvent(pObject, pFunction, pParms);
		CombatPostProcessEvent(pObject, pFunction, pParms);
		return;
	}

	if (TryBlockExploitProcessEvent(pObject, pFunction, pParms))
		return;

	CombatPreProcessEvent(pObject, pFunction, pParms);

	static thread_local DWORD s_routedGameTid = 0;
	static thread_local bool s_onGameThread = false;
	const DWORD gameTid = g_GameThreadId.load(std::memory_order_acquire);
	if (gameTid != 0 && gameTid != s_routedGameTid)
	{
		s_routedGameTid = gameTid;
		s_onGameThread = (GetCurrentThreadId() == gameTid);
	}

	if (!s_onGameThread)
	{
		SafeForwardProcessEvent(pObject, pFunction, pParms);
		CombatPostProcessEvent(pObject, pFunction, pParms);
		return;
	}

	if (!g_gatherRequested.load(std::memory_order_acquire))
	{
		static thread_local uint32_t s_idlePeCounter = 0;
		if (cheat && (++s_idlePeCounter & 31) == 0 && g_menuInputLockRequested.exchange(false))
		{
			struct GatherGuard
			{
				GatherGuard() { g_inGameThreadFlush = true; }
				~GatherGuard() { g_inGameThreadFlush = false; }
			} gatherGuard;
			TryApplyMenuInputLock(cheat, g_menuInputLockOpen.load());
		}
		SafeForwardProcessEvent(pObject, pFunction, pParms);
		CombatPostProcessEvent(pObject, pFunction, pParms);
		return;
	}

	if (cheat && g_gatherRequested.exchange(false, std::memory_order_acq_rel))
	{
		struct GatherGuard
		{
			GatherGuard() { g_inGameThreadFlush = true; }
			~GatherGuard() { g_inGameThreadFlush = false; }
		} gatherGuard;
		TryRunGameThreadInit(cheat);
	}

	static thread_local uint32_t s_gamePeCounter = 0;
	if (cheat && (++s_gamePeCounter & 31) == 0 && g_menuInputLockRequested.exchange(false))
	{
		struct GatherGuard
		{
			GatherGuard() { g_inGameThreadFlush = true; }
			~GatherGuard() { g_inGameThreadFlush = false; }
		} gatherGuard;
		TryApplyMenuInputLock(cheat, g_menuInputLockOpen.load());
	}

	SafeForwardProcessEvent(pObject, pFunction, pParms);
	CombatPostProcessEvent(pObject, pFunction, pParms);
}

bool init = false;
static DXGI_FORMAT g_swapChainFormat = DXGI_FORMAT_UNKNOWN;
// Controls whether the hook is active. Set to false to disable the hook and restore the original Present() function.
static std::atomic<bool> bRunning(true);
// Guards Unload() so it runs exactly once - both the unload hotkey and
// DLL_PROCESS_DETACH funnel through it, and tearing down twice would
// double-disable MinHook and double-release the D3D12 resources.
static std::atomic<bool> bUnloaded(false);

HRESULT __stdcall hkPresent(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags)
{
	// The streamproof overlay swapchain shares this same hooked Present vtable entry.
	// OverlayWindow::RenderAndPresent() calls its own swapchain's Present(), which
	// re-enters here; without this early pass-through we'd recurse (hook -> render ->
	// present -> hook ...) until the stack overflows. Present the overlay via the
	// original Present and return immediately.
	if (OverlayWindow::IsOverlaySwapChain(pSwapChain))
		return oPresent(pSwapChain, SyncInterval, Flags);

	// Resolve the game thread by name, retrying each frame until found (it's named by the time we're
	// injected, so this normally succeeds on frame 1). Once-per-frame keeps the thread-snapshot scan
	// off the hot ProcessEvent path. Latched for the rest of the session via compare_exchange.
	if (g_GameThreadId.load() == 0)
	{
		DWORD gameTid = FindGameThreadId();
		if (gameTid != 0)
		{
			DWORD expected = 0;
			if (g_GameThreadId.compare_exchange_strong(expected, gameTid))
				PhLog("[hkPresent] Game thread TID: %lu\n", static_cast<unsigned long>(gameTid));
		}
	}

	if (!bRunning)
		return oPresent(pSwapChain, SyncInterval, Flags);
	if (!init)
	{
		if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&DirectX12Interface::Device)))
		{
			// Texture uploads must go through the game's DIRECT queue; wait until the hook has captured it.
			if (!DirectX12Interface::CommandQueue)
				return oPresent(pSwapChain, SyncInterval, Flags);

			IMGUI_CHECKVERSION();
			ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			(void)io;
			ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantTextInput || ImGui::GetIO().WantCaptureKeyboard;
			io.IniFilename = NULL;
			io.ConfigNavMoveSetMousePos = true;
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

			// Load Unicode fonts so non-ASCII player names (Chinese, Arabic, Cyrillic, etc.) render correctly.
			// Segoe UI ships on all Windows 10/11 and covers Latin, Cyrillic, Greek, Arabic, Hebrew, Thai, and more.
			// CJK fonts are merged in when present; AddFontFromFileTTF silently returns nullptr if the file is missing.
			{
				ImFontConfig cfg;
				cfg.OversampleH = 1;
				cfg.OversampleV = 1;
				// imgui 1.92+ logs/asserts when a font file is missing instead of silently
				// returning nullptr. These fonts are best-effort (e.g. CJK files may not exist),
				// so suppress the load error and rely on the return-value checks below.
				cfg.Flags |= ImFontFlags_NoLoadError;

				const float kFontSize = 16.0f;
				if (io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", kFontSize, &cfg, io.Fonts->GetGlyphRangesDefault()))
				{
					cfg.MergeMode = true;
					io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", kFontSize, &cfg, io.Fonts->GetGlyphRangesCyrillic());
					io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", kFontSize, &cfg, io.Fonts->GetGlyphRangesGreek());
					static const ImWchar arabic_ranges[] = { 0x0600, 0x06FF, 0xFB50, 0xFDFF, 0xFE70, 0xFEFF, 0 };
					io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", kFontSize, &cfg, arabic_ranges);
					io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", kFontSize, &cfg, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
					io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\meiryo.ttc", kFontSize, &cfg, io.Fonts->GetGlyphRangesJapanese());
					io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf", kFontSize, &cfg, io.Fonts->GetGlyphRangesKorean());
				}

				// Merge Font Awesome 6 (solid) so tab/button labels can use icon glyphs.
				// Ships alongside peterhack.dll in fonts\; skipped cleanly if missing.
				const std::string faPath = ResolveAssetPath(L"fa-solid-900.ttf");
				if (!faPath.empty())
				{
					static const ImWchar iconRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
					ImFontConfig iconCfg;
					iconCfg.MergeMode = true;
					iconCfg.PixelSnapH = true;
					iconCfg.Flags |= ImFontFlags_NoLoadError;
					// Nudge glyphs down a touch so they sit on the text baseline.
					iconCfg.GlyphOffset = ImVec2(0.0f, 1.0f);
					iconCfg.GlyphMinAdvanceX = kFontSize; // keep icons monospaced-ish
					io.Fonts->AddFontFromFileTTF(faPath.c_str(), kFontSize - 2.0f, &iconCfg, iconRanges);
				}
				else
				{
					PhLog("[UI] Font Awesome icon font not found; icons will show as boxes.\n");
				}
			}

			// Check if the font was loaded successfully
			if (io.Fonts->Fonts.empty())
			{
				PhLog("Failed to load fonts. Falling back to default font.\n");
				io.Fonts->AddFontDefault();
			}

			DXGI_SWAP_CHAIN_DESC Desc;
			pSwapChain->GetDesc(&Desc);
			Process::Hwnd = Desc.OutputWindow; // use the window the swapchain actually presents to, not GetForegroundWindow()'s guess
			Gamepad::SetWindow(Process::Hwnd); // DirectInput devices bind to the game window
			Desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
			Desc.Windowed = ((GetWindowLongPtr(Process::Hwnd, GWL_STYLE) & WS_POPUP) != 0) ? false : true;
			const DXGI_FORMAT swapChainFormat = Desc.BufferDesc.Format;
			g_swapChainFormat = swapChainFormat;

			DirectX12Interface::BuffersCounts = Desc.BufferCount;
			DirectX12Interface::FrameContext = new DirectX12Interface::_FrameContext[DirectX12Interface::BuffersCounts];

			D3D12_DESCRIPTOR_HEAP_DESC DescriptorImGuiRender = {};
			DescriptorImGuiRender.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			DescriptorImGuiRender.NumDescriptors = DirectX12Interface::ImGuiSrvCapacity;
			DescriptorImGuiRender.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

			if (DirectX12Interface::Device->CreateDescriptorHeap(&DescriptorImGuiRender, IID_PPV_ARGS(&DirectX12Interface::DescriptorHeapImGuiRender)) != S_OK)
				return oPresent(pSwapChain, SyncInterval, Flags);

			DirectX12Interface::ImGuiSrvDescriptorSize = DirectX12Interface::Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			DirectX12Interface::ImGuiSrvCpuStart = DirectX12Interface::DescriptorHeapImGuiRender->GetCPUDescriptorHandleForHeapStart();
			DirectX12Interface::ImGuiSrvGpuStart = DirectX12Interface::DescriptorHeapImGuiRender->GetGPUDescriptorHandleForHeapStart();
			DirectX12Interface::ResetImGuiSrvAllocator();

			// One command allocator per frame context. Sharing a single allocator and
			// resetting it every frame corrupts state while the GPU is still executing the
			// previous frame's commands; a per-context allocator gated by the fence below
			// is only reset once the GPU is provably done with it.
			for (size_t i = 0; i < DirectX12Interface::BuffersCounts; i++)
			{
				if (DirectX12Interface::Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&DirectX12Interface::FrameContext[i].CommandAllocator)) != S_OK)
					return oPresent(pSwapChain, SyncInterval, Flags);
				DirectX12Interface::FrameContext[i].FenceValue = 0;
			}

			if (DirectX12Interface::Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, DirectX12Interface::FrameContext[0].CommandAllocator, NULL, IID_PPV_ARGS(&DirectX12Interface::CommandList)) != S_OK ||
				DirectX12Interface::CommandList->Close() != S_OK)
				return oPresent(pSwapChain, SyncInterval, Flags);

			// Fence + event survive resizes (init reruns), so guard creation to avoid leaking
			// one per resize. FenceLastSignaledValue keeps counting across re-inits.
			if (!DirectX12Interface::Fence)
			{
				if (DirectX12Interface::Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&DirectX12Interface::Fence)) != S_OK)
					return oPresent(pSwapChain, SyncInterval, Flags);

				DirectX12Interface::FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
				if (!DirectX12Interface::FenceEvent)
					return oPresent(pSwapChain, SyncInterval, Flags);
			}

			D3D12_DESCRIPTOR_HEAP_DESC DescriptorBackBuffers;
			DescriptorBackBuffers.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			DescriptorBackBuffers.NumDescriptors = DirectX12Interface::BuffersCounts;
			DescriptorBackBuffers.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			DescriptorBackBuffers.NodeMask = 1;

			if (DirectX12Interface::Device->CreateDescriptorHeap(&DescriptorBackBuffers, IID_PPV_ARGS(&DirectX12Interface::DescriptorHeapBackBuffers)) != S_OK)
			{
				return oPresent(pSwapChain, SyncInterval, Flags);
			}

			const auto RTVDescriptorSize = DirectX12Interface::Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			D3D12_CPU_DESCRIPTOR_HANDLE RTVHandle = DirectX12Interface::DescriptorHeapBackBuffers->GetCPUDescriptorHandleForHeapStart();

			for (size_t i = 0; i < DirectX12Interface::BuffersCounts; i++)
			{
				ID3D12Resource* pBackBuffer = nullptr;
				DirectX12Interface::FrameContext[i].DescriptorHandle = RTVHandle;
				pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
				DirectX12Interface::Device->CreateRenderTargetView(pBackBuffer, nullptr, RTVHandle);
				DirectX12Interface::FrameContext[i].Resource = pBackBuffer;
				RTVHandle.ptr += RTVDescriptorSize;
			}

			ImGui_ImplWin32_EnableDpiAwareness();
			ImGui_ImplWin32_Init(Process::Hwnd);

			ImGui_ImplDX12_InitInfo dx12Init{};
			dx12Init.Device = DirectX12Interface::Device;
			dx12Init.CommandQueue = DirectX12Interface::CommandQueue;
			dx12Init.NumFramesInFlight = static_cast<int>(DirectX12Interface::BuffersCounts);
			dx12Init.RTVFormat = swapChainFormat;
			dx12Init.SrvDescriptorHeap = DirectX12Interface::DescriptorHeapImGuiRender;
			dx12Init.SrvDescriptorAllocFn = DirectX12Interface::ImGuiSrvDescriptorAlloc;
			dx12Init.SrvDescriptorFreeFn = DirectX12Interface::ImGuiSrvDescriptorFree;
			if (!ImGui_ImplDX12_Init(&dx12Init))
				return oPresent(pSwapChain, SyncInterval, Flags);
			ImGui_ImplDX12_CreateDeviceObjects();
			// ImGui::GetIO().ImeWindowHandle = Process::Hwnd;
			ImGui::GetMainViewport()->PlatformHandleRaw = Process::Hwnd;
			// Only hook WndProc once - on resize init reruns, Process::WndProc already holds
			// the original game proc. Hooking again would overwrite it with our own hook,
			// causing CallWindowProc -> WndProc -> CallWindowProc infinite recursion.
			if (!Process::WndProc)
				Process::WndProc = (WNDPROC)SetWindowLongPtr(Process::Hwnd, GWLP_WNDPROC, (__int3264)(LONG_PTR)WndProc);

			// Mark initialized only after every resource above was created - any failed step
			// returns early and leaves init false, so the next frame retries cleanly instead
			// of falling through to the render path with null D3D state.
			init = true;
		}
	}

	// Init hasn't completed (device not ready, or a create step failed this frame) - don't
	// run the render path against half-built state.
	if (!init)
		return oPresent(pSwapChain, SyncInterval, Flags);

	if (DirectX12Interface::CommandQueue == nullptr)
		return oPresent(pSwapChain, SyncInterval, Flags);

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
	ImGui::Begin(("##scene"), nullptr, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar);

	auto& io = ImGui::GetIO();
	ImGui::SetWindowPos(ImVec2(0, 0), ImGuiCond_Always);
	ImGui::SetWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y), ImGuiCond_Always);

	if (cheat && cheat->NeedsGameThreadTick())
	{
		// Game-thread scan + tool exploits (survivor/hunter toggles, kill, teleport, etc.).
		g_gatherRequested.store(true);
	}

	// Streamproof renders ImGui on a separate topmost overlay window (excluded from
	// capture). The game swapchain is left untouched so OBS/Discord still show gameplay.
	const bool streamproofOverlay = cfg && cfg->bStreamproof;
	if (streamproofOverlay && DirectX12Interface::Device && DirectX12Interface::CommandQueue)
	{
		if (!OverlayWindow::IsReady())
		{
			OverlayWindow::EnsureInitialized(
				DirectX12Interface::Device,
				DirectX12Interface::CommandQueue,
				pSwapChain,
				Process::Hwnd,
				g_swapChainFormat != DXGI_FORMAT_UNKNOWN ? g_swapChainFormat : DXGI_FORMAT_R8G8B8A8_UNORM);
		}
		if (OverlayWindow::IsReady())
		{
			OverlayWindow::SyncPosition(Process::Hwnd);
			OverlayWindow::SetVisible(true);
			OverlayWindow::SetCaptureExcluded(true);
		}
	}
	else
	{
		OverlayWindow::SetCaptureExcluded(false);
		OverlayWindow::SetVisible(false);
	}

	// Never exclude the game window from capture — only the private overlay HWND.
	if (Process::Hwnd)
		SetWindowDisplayAffinity(Process::Hwnd, 0x00000000u);

	if (cheat && (cfg->bMenuOpen || g_menuInputLockApplied.load(std::memory_order_acquire)))
	{
		g_menuInputLockOpen.store(cfg->bMenuOpen);
		g_menuInputLockRequested.store(true);
	}

	if (cfg->bInitHooks && cheat)
		cheat->RenderEsp();

	// Sample the controller once per frame; every pad bind (menu, magnet, camo
	// hotkeys, recorder capture) does edge detection off this single poll.
	Gamepad::Poll(cfg->bControllerBinds || CamoUsesPadHotkeys());
	Gamepad::SetReservedButton(Binds::IsPadBind(cfg->iControllerMenuButton)
		? Binds::PadMask(cfg->iControllerMenuButton) : 0);

	// ignore hotkeys if the game window isn't focused, or if the user is typing in a text input (chat, console, etc.)
	// Magnet hotkey toggles in-match active state; menu checkbox must stay enabled.
	if (!cfg->bMenuOpen && IsGameWindowFocused() && !ImGui::GetIO().WantTextInput &&
		cfg->bMagnetEnabled && Binds::Pressed(cfg->iMagnetKey))
	{
		cfg->bMagnetActive = !cfg->bMagnetActive;
		if (cfg->bNotifications)
			Notify::Info(cfg->bMagnetActive ? "Magnet on" : "Magnet off");
	}

	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);

	if (IsGameWindowFocused() &&
		((GetAsyncKeyState(VK_INSERT) & 1) || (GetAsyncKeyState(VK_F10) & 1) ||
		 Binds::Pressed(cfg->iControllerMenuButton, true)))
		cfg->bMenuOpen = !cfg->bMenuOpen;

	if (g_camo && !cfg->bMenuOpen)
	{
		const bool inMatch = cheat ? cheat->inMatchCached.load(std::memory_order_acquire) : false;
		g_camo->TickHotkeys(inMatch, cfg->bMenuOpen);
	}

	static bool prevMenuOpen = false;
	ImGuiIO& menuIo = ImGui::GetIO();
	menuIo.MouseDrawCursor = cfg->bMenuOpen;
	menuIo.ConfigNavMoveSetMousePos = false;

	if (cfg->bMenuOpen && !prevMenuOpen)
	{
		ReleaseLocalMouseCapture();
		SyncMenuMousePosition(menuIo);
	}

	if (cfg->bMenuOpen)
	{
		if (!prevMenuOpen && g_camo)
			g_camo->OnMenuOpened();
		static bool menuStyleApplied = false;
		if (!menuStyleApplied)
		{
			Theme::ApplyDeepDark();
			menuStyleApplied = true;
		}
		gui->Init();
	}

	// Persistent overlays (drawn on the foreground draw list, above the menu).
	if (cfg && gui && cfg->bStatusHud)
		gui->DrawHud();
	if (cfg && cfg->bNotifications)
		Notify::Draw();

	if (prevMenuOpen && !cfg->bMenuOpen)
	{
		RestoreSystemMouseCursor();
		if (Process::Hwnd)
			SetForegroundWindow(Process::Hwnd);
		Binds::CancelRecorder(); // abort any in-progress key/pad capture
		Binds::ClearKeyEdges(); // swallow stale magnet/menu key edges after closing UI
		if (g_camo)
			g_camo->ClearHotkeyEdges();
		memset(menuIo.MouseDown, 0, sizeof(menuIo.MouseDown));
		menuIo.MouseWheel = 0.0f;
		menuIo.MouseWheelH = 0.0f;
		menuIo.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
	}
	prevMenuOpen = cfg->bMenuOpen;

	ImGui::EndFrame();
	ImGui::Render();

	// Streamproof path: cheat UI on the private overlay swapchain; game frame is clean.
	if (streamproofOverlay && OverlayWindow::IsReady())
	{
		OverlayWindow::RenderAndPresent(
			DirectX12Interface::CommandList,
			DirectX12Interface::CommandQueue,
			DirectX12Interface::DescriptorHeapImGuiRender,
			DirectX12Interface::Fence,
			DirectX12Interface::FenceLastSignaledValue,
			DirectX12Interface::FenceEvent,
			ImGui::GetDrawData());
		return oPresent(pSwapChain, SyncInterval, Flags);
	}

	DirectX12Interface::_FrameContext& CurrentFrameContext = DirectX12Interface::FrameContext[pSwapChain->GetCurrentBackBufferIndex()];

	// Wait for the GPU to finish the last frame that used this context's allocator before
	// resetting it — resetting an allocator with in-flight commands is undefined behaviour.
	// Bounded wait so a wedged/removed GPU can't hard-hang the game's render thread.
	if (DirectX12Interface::Fence->GetCompletedValue() < CurrentFrameContext.FenceValue)
	{
		DirectX12Interface::Fence->SetEventOnCompletion(CurrentFrameContext.FenceValue, DirectX12Interface::FenceEvent);
		WaitForSingleObject(DirectX12Interface::FenceEvent, 2000);
	}

	CurrentFrameContext.CommandAllocator->Reset();

	D3D12_RESOURCE_BARRIER Barrier;
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	Barrier.Transition.pResource = CurrentFrameContext.Resource;
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

	DirectX12Interface::CommandList->Reset(CurrentFrameContext.CommandAllocator, nullptr);
	DirectX12Interface::CommandList->ResourceBarrier(1, &Barrier);
	DirectX12Interface::CommandList->OMSetRenderTargets(1, &CurrentFrameContext.DescriptorHandle, FALSE, nullptr);
	DirectX12Interface::CommandList->SetDescriptorHeaps(1, &DirectX12Interface::DescriptorHeapImGuiRender);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), DirectX12Interface::CommandList);
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	DirectX12Interface::CommandList->ResourceBarrier(1, &Barrier);
	DirectX12Interface::CommandList->Close();
	DirectX12Interface::CommandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(&DirectX12Interface::CommandList));

	// Signal the fence on the queue and record the value for this frame context, so next time
	// we cycle back to it we know when the GPU has finished this frame's commands.
	const UINT64 signalValue = ++DirectX12Interface::FenceLastSignaledValue;
	DirectX12Interface::CommandQueue->Signal(DirectX12Interface::Fence, signalValue);
	CurrentFrameContext.FenceValue = signalValue;

	return oPresent(pSwapChain, SyncInterval, Flags);
}

void __stdcall hkExecuteCommandLists(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists)
{
	// Only capture the DIRECT (graphics) queue — the game also runs copy/compute queues, and
	// submitting our graphics command list to one of those would be an invalid submission and
	// remove the device. When the device is already known, also require the queue to belong to
	// it, so on a multi-adapter machine (iGPU + dGPU) we don't grab a DIRECT queue from the
	// wrong device. (Best effort: ExecuteCommandLists often fires before Present sets Device,
	// in which case we fall back to capturing the first DIRECT queue, as before.)
	if (!DirectX12Interface::CommandQueue && queue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
	{
		bool sameDevice = true;
		if (DirectX12Interface::Device)
		{
			ID3D12Device* queueDevice = nullptr;
			if (SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))))
			{
				sameDevice = (queueDevice == DirectX12Interface::Device);
				queueDevice->Release();
			}
		}

		if (sameDevice)
			DirectX12Interface::CommandQueue = queue;
	}

	return oExecuteCommandLists(queue, NumCommandLists, ppCommandLists);
}

// Wait until the GPU has finished every frame we've submitted, reusing the persistent render
// fence, so the command list and per-frame allocators can be released without freeing memory
// the GPU is still reading. Bounded so a wedged GPU can't hang teardown forever.
static void WaitForGpuIdle()
{
	if (!DirectX12Interface::Fence || !DirectX12Interface::FenceEvent)
		return;

	const UINT64 value = DirectX12Interface::FenceLastSignaledValue;
	if (value != 0 && DirectX12Interface::Fence->GetCompletedValue() < value)
	{
		DirectX12Interface::Fence->SetEventOnCompletion(value, DirectX12Interface::FenceEvent);
		WaitForSingleObject(DirectX12Interface::FenceEvent, 1000);
	}
}

HRESULT __stdcall hkResizeBuffers(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
	if (!bRunning)
		return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

	if (OverlayWindow::IsReady() && Width > 0 && Height > 0)
		OverlayWindow::OnGameResize(Width, Height);

	if (init)
	{
		// Make sure the GPU is done with our command list / allocators before freeing them.
		WaitForGpuIdle();

		// Backends must be shut down before the context is destroyed
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		// The command list is recreated on the next hkPresent init, so drop the old one.
		if (DirectX12Interface::CommandList)
		{
			DirectX12Interface::CommandList->Release();
			DirectX12Interface::CommandList = nullptr;
		}

		// Release per-frame back buffer resources (DXGI requires all GetBuffer() references
		// dropped before ResizeBuffers) and the per-frame command allocators, which hkPresent
		// recreates after the resize.
		for (size_t i = 0; i < DirectX12Interface::BuffersCounts; i++)
		{
			if (DirectX12Interface::FrameContext[i].Resource)
			{
				DirectX12Interface::FrameContext[i].Resource->Release();
				DirectX12Interface::FrameContext[i].Resource = nullptr;
			}
			if (DirectX12Interface::FrameContext[i].CommandAllocator)
			{
				DirectX12Interface::FrameContext[i].CommandAllocator->Release();
				DirectX12Interface::FrameContext[i].CommandAllocator = nullptr;
			}
		}
		delete[] DirectX12Interface::FrameContext;
		DirectX12Interface::FrameContext = nullptr;

		if (DirectX12Interface::DescriptorHeapBackBuffers)
		{
			DirectX12Interface::DescriptorHeapBackBuffers->Release();
			DirectX12Interface::DescriptorHeapBackBuffers = nullptr;
		}

		if (DirectX12Interface::DescriptorHeapImGuiRender)
		{
			DirectX12Interface::DescriptorHeapImGuiRender->Release();
			DirectX12Interface::DescriptorHeapImGuiRender = nullptr;
		}
		DirectX12Interface::ResetImGuiSrvAllocator();

		// Device is NOT tied to swap chain buffers and must not be released here
		init = false;
	}

	return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

void Unload()
{
	// Run the teardown exactly once, no matter how many paths call us.
	bool expected = false;
	if (!bUnloaded.compare_exchange_strong(expected, true))
		return;

	// Signal hooks to bail out immediately, then wait long enough for any call
	// that is already mid-execution on the render thread to return before we
	// start freeing shared state. MH_DisableHook only blocks future calls -
	// it cannot stop one that is already inside hkPresent/hkResizeBuffers.
	bRunning = false;
	Sleep(100);

	// Restore the original WndProc before this module is unmapped, otherwise the window
	// keeps a dangling pointer into our WndProc and crashes on the next message.
	if (Process::WndProc)
		SetWindowLongPtr(Process::Hwnd, GWLP_WNDPROC, (__int3264)(LONG_PTR)Process::WndProc);

	// Release DirectInput controller devices before the module goes away.
	Gamepad::Shutdown();

	OverlayWindow::Shutdown();

	// MinHook
	MH_DisableHook(MH_ALL_HOOKS);
	MH_Uninitialize();

	// ImGui + D3D12 resources
	if (init)
	{
		// Make sure the GPU is done with our command list / allocators before freeing them.
		WaitForGpuIdle();

		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		if (DirectX12Interface::CommandList)
		{
			DirectX12Interface::CommandList->Release();
			DirectX12Interface::CommandList = nullptr;
		}

		// Release back buffer COM references so the game's ResizeBuffers can succeed
		// after re-injection. hkPresent re-acquires these after every resize, so they
		// must be explicitly released here — not doing so leaks the references. The
		// per-frame command allocators are owned by us too and need releasing.
		if (DirectX12Interface::FrameContext)
		{
			for (size_t i = 0; i < DirectX12Interface::BuffersCounts; i++)
			{
				if (DirectX12Interface::FrameContext[i].Resource)
					DirectX12Interface::FrameContext[i].Resource->Release();
				if (DirectX12Interface::FrameContext[i].CommandAllocator)
					DirectX12Interface::FrameContext[i].CommandAllocator->Release();
			}
			delete[] DirectX12Interface::FrameContext;
			DirectX12Interface::FrameContext = nullptr;
		}

		if (DirectX12Interface::DescriptorHeapBackBuffers)
		{
			DirectX12Interface::DescriptorHeapBackBuffers->Release();
			DirectX12Interface::DescriptorHeapBackBuffers = nullptr;
		}

		if (DirectX12Interface::DescriptorHeapImGuiRender)
		{
			DirectX12Interface::DescriptorHeapImGuiRender->Release();
			DirectX12Interface::DescriptorHeapImGuiRender = nullptr;
		}
		DirectX12Interface::ResetImGuiSrvAllocator();
	}

	// Fence + event persist across resizes (guarded creation), so release them once here.
	if (DirectX12Interface::Fence)
	{
		DirectX12Interface::Fence->Release();
		DirectX12Interface::Fence = nullptr;
	}
	if (DirectX12Interface::FenceEvent)
	{
		CloseHandle(DirectX12Interface::FenceEvent);
		DirectX12Interface::FenceEvent = nullptr;
	}

	// misc
	// Detach the CRT's stdio from the console before freeing it. While the
	// freopen_s'd CONOUT$/CONIN$ handles stay open the console host keeps the
	// window alive, so FreeConsole alone leaves it lingering after ejecting.
	FILE* Dummy;
	freopen_s(&Dummy, "NUL", "w", stdout);
	freopen_s(&Dummy, "NUL", "w", stderr);
	freopen_s(&Dummy, "NUL", "r", stdin);
	FreeConsole();
}

void InitProcess(bool* WindowFocus)
{
	DWORD ForegroundWindowProcessID;
	GetWindowThreadProcessId(GetForegroundWindow(), &ForegroundWindowProcessID);
	if (GetCurrentProcessId() == ForegroundWindowProcessID)
	{

		Process::ID = GetCurrentProcessId();
		Process::Handle = GetCurrentProcess();
		Process::Hwnd = GetForegroundWindow();

		RECT TempRect;
		GetWindowRect(Process::Hwnd, &TempRect);
		Process::WindowWidth = TempRect.right - TempRect.left;
		Process::WindowHeight = TempRect.bottom - TempRect.top;

		*WindowFocus = true;
	}
}

DWORD MainThread(HMODULE Module)
{
	PhInitConsole();

	_mkdir("C:\\peterhack");

	// Initialize global instances
	cfg = new Settings();
	if (!cfg)
		return 0;

	cheat = new CheatManager();
	if (!cheat)
		return 0;

	gui = new Menu();
	if (!gui)
		return 0;

	g_camo = new CamoManager();
	if (!g_camo)
		return 0;
	g_camo->settings.Load();

	draw = new Drawings();
	if (!draw)
		return 0;

	cfg->LoadSettings();

	// Auto-load a named profile at startup if the user opted in (Config tab).
	if (cfg->bAutoLoadProfile && cfg->szAutoLoadProfile[0] != '\0')
	{
		if (cfg->LoadProfile(cfg->szAutoLoadProfile))
			PhLog("Auto-loaded profile: %s\n", cfg->szAutoLoadProfile);
		else
			PhLog("Auto-load profile failed: %s\n", cfg->szAutoLoadProfile);
	}

	// Wait for the game window to be in focus before proceeding
	bool WindowFocus = false;
	while (WindowFocus == false)
	{
		InitProcess(&WindowFocus);
		Sleep(100);
	}

	if (MH_Initialize() != MH_OK)
	{
		PhLog("Failed to initialize MinHook!\n");
		return 1;
	}

	kiero::D3D12Output d3d12;
	while (true)
	{
		auto err = kiero::locate<kiero::Implementation_D3D12>(nullptr, &d3d12);
		if (err == kiero::Error_Nil)
		{
			PhLog("DirectX 12 interface located!\n");
			break;
		}
		Sleep(100);
	}

	void* tExecuteCommandLists = d3d12.command_queue_methods[10];
	void* tPresent = d3d12.swapchain_methods[8];
	void* tResizeBuffers = d3d12.swapchain_methods[13];

	if (MH_CreateHook(tExecuteCommandLists, hkExecuteCommandLists, (LPVOID*)&oExecuteCommandLists) != MH_OK)
	{
		PhLog("Failed to hook ExecuteCommandLists!\n");
	}

	if (MH_CreateHook(tPresent, hkPresent, (LPVOID*)&oPresent) != MH_OK)
	{
		PhLog("Failed to hook Present!\n");
	}

	if (MH_CreateHook(tResizeBuffers, hkResizeBuffers, (LPVOID*)&oResizeBuffers) != MH_OK)
	{
		PhLog("Failed to hook ResizeBuffers!\n");
	}

	void* tProcessEvent = reinterpret_cast<void*>(SDK::InSDKUtils::GetImageBase() + SDK::Offsets::ProcessEvent);
	if (!ProcessEventHookTargetLooksValid(tProcessEvent))
	{
		PhLog("ProcessEvent hook target looks invalid at %p (offset 0x%X)\n",
			tProcessEvent, SDK::Offsets::ProcessEvent);
	}
	else if (MH_CreateHook(tProcessEvent, hkProcessEvent, (LPVOID*)&oProcessEvent) != MH_OK)
	{
		PhLog("Failed to hook ProcessEvent!\n");
	}

	MH_EnableHook(MH_ALL_HOOKS);
	PhLog("[INIT] ImageBase=%p ProcessEvent=%p (offset 0x%X)\n",
	      reinterpret_cast<void*>(SDK::InSDKUtils::GetImageBase()),
	      tProcessEvent,
	      SDK::Offsets::ProcessEvent);
	PhLog("[INIT] GObjects=0x%X GNames=0x%X GWorld=0x%X\n",
	      SDK::Offsets::GObjects,
	      SDK::Offsets::GNames,
	      SDK::Offsets::GWorld);
	PhLog("[INIT] Hooks enabled: ExecuteCommandLists Present ResizeBuffers ProcessEvent\n");
	// Resolve UFunction pointers lazily on the game thread (Refresh*IfStale in TryRunGameThreadInit).
	// Eager StaticClass lookups here crashed on the new Shipping build when legacy BP classes are absent.

	// poll for the END key to be pressed to unload the DLL
	while (bRunning)
	{
		if ((GetAsyncKeyState(VK_END) & 1) && IsGameWindowFocused())
			break;
		Sleep(50);
	}

	Unload();
	FreeLibraryAndExitThread(Process::Module, 0);

	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		Process::Module = hModule;
		CreateThread(0, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0, 0);
		break;
	case DLL_PROCESS_DETACH:
		Unload();
		break;
	}

	return TRUE;
}
