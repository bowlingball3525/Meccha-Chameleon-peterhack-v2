#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

struct ImDrawData;

// Separate topmost transparent window for ImGui when streamproof is enabled.
// OBS/Discord game capture reads the game swapchain (no cheat UI). This overlay
// is excluded from capture via WDA_EXCLUDEFROMCAPTURE on its own HWND only.
namespace OverlayWindow
{
	bool IsReady();

	// True when the given swapchain is our private overlay swapchain. The overlay
	// shares the game's hooked Present vtable, so hkPresent uses this to pass the
	// overlay's own Present straight through instead of re-running the cheat path
	// (which would recurse infinitely into RenderAndPresent -> Present -> hook).
	bool IsOverlaySwapChain(IDXGISwapChain* swapChain);

	void EnsureInitialized(ID3D12Device* device,
		ID3D12CommandQueue* queue,
		IDXGISwapChain3* gameSwapChain,
		HWND gameHwnd,
		DXGI_FORMAT format);

	void SyncPosition(HWND gameHwnd);
	void SetVisible(bool visible);
	void SetCaptureExcluded(bool excluded);

	// Draw ImGui to the overlay swapchain and present. Does not touch the game back buffer.
	void RenderAndPresent(ID3D12GraphicsCommandList* commandList,
		ID3D12CommandQueue* queue,
		ID3D12DescriptorHeap* imguiSrvHeap,
		ID3D12Fence* fence,
		UINT64& fenceCounter,
		HANDLE fenceEvent,
		ImDrawData* drawData);

	void OnGameResize(UINT width, UINT height);
	void Shutdown();
}
