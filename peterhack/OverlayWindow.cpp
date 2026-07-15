#include "OverlayWindow.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx12.h"

#include <algorithm>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace
{
	constexpr wchar_t kOverlayClassName[] = L"PHTopOverlay";
	constexpr UINT kBufferCount = 2;

	HWND g_overlayHwnd = nullptr;
	IDXGISwapChain3* g_swapChain = nullptr;
	ID3D12Device* g_device = nullptr;
	ID3D12DescriptorHeap* g_rtvHeap = nullptr;
	UINT g_rtvDescriptorSize = 0;

	struct OverlayFrame
	{
		ID3D12CommandAllocator* commandAllocator = nullptr;
		ID3D12Resource* backBuffer = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
		UINT64 fenceValue = 0;
	};

	OverlayFrame g_frames[kBufferCount]{};
	bool g_initialized = false;
	bool g_classRegistered = false;

	void ReleaseFrameResources()
	{
		for (UINT i = 0; i < kBufferCount; ++i)
		{
			if (g_frames[i].backBuffer)
			{
				g_frames[i].backBuffer->Release();
				g_frames[i].backBuffer = nullptr;
			}
			if (g_frames[i].commandAllocator)
			{
				g_frames[i].commandAllocator->Release();
				g_frames[i].commandAllocator = nullptr;
			}
			g_frames[i].fenceValue = 0;
		}
	}

	bool CreateOverlayHwnd(HWND gameHwnd)
	{
		HINSTANCE inst = GetModuleHandleW(nullptr);
		if (!g_classRegistered)
		{
			WNDCLASSEXW wc{};
			wc.cbSize = sizeof(wc);
			wc.lpfnWndProc = DefWindowProcW;
			wc.hInstance = inst;
			wc.lpszClassName = kOverlayClassName;
			if (!RegisterClassExW(&wc))
				return false;
			g_classRegistered = true;
		}

		RECT rc{};
		GetClientRect(gameHwnd, &rc);
		POINT origin{ 0, 0 };
		ClientToScreen(gameHwnd, &origin);
		const int w = rc.right > rc.left ? (rc.right - rc.left) : 1;
		const int h = rc.bottom > rc.top ? (rc.bottom - rc.top) : 1;

		g_overlayHwnd = CreateWindowExW(
			WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
			kOverlayClassName,
			L"",
			WS_POPUP,
			origin.x, origin.y, w, h,
			nullptr, nullptr, inst, nullptr);
		if (!g_overlayHwnd)
			return false;

		// Per-pixel alpha from the D3D clear + ImGui blend.
		SetLayeredWindowAttributes(g_overlayHwnd, 0, 255, LWA_ALPHA);
		MARGINS margins{ -1, -1, -1, -1 };
		DwmExtendFrameIntoClientArea(g_overlayHwnd, &margins);
		ShowWindow(g_overlayHwnd, SW_SHOWNOACTIVATE);
		return true;
	}

	bool CreateOverlaySwapChain(ID3D12CommandQueue* queue, IDXGISwapChain3* gameSwapChain, DXGI_FORMAT format, UINT width, UINT height)
	{
		IDXGIFactory4* factory = nullptr;
		if (FAILED(gameSwapChain->GetParent(IID_PPV_ARGS(&factory))))
			return false;

		factory->MakeWindowAssociation(g_overlayHwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

		DXGI_SWAP_CHAIN_DESC1 desc{};
		desc.Width = width;
		desc.Height = height;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = kBufferCount;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

		IDXGISwapChain1* sc1 = nullptr;
		HRESULT hr = factory->CreateSwapChainForHwnd(
			queue, g_overlayHwnd, &desc, nullptr, nullptr, &sc1);
		if (FAILED(hr))
		{
			// Fallback for drivers that reject premultiplied alpha swap chains.
			desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
			hr = factory->CreateSwapChainForHwnd(
				queue, g_overlayHwnd, &desc, nullptr, nullptr, &sc1);
		}
		factory->Release();
		if (FAILED(hr))
			return false;

		sc1->QueryInterface(IID_PPV_ARGS(&g_swapChain));
		sc1->Release();
		return g_swapChain != nullptr;
	}

	bool CreateOverlayRenderTargets(DXGI_FORMAT format)
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heapDesc.NumDescriptors = kBufferCount;
		if (FAILED(g_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&g_rtvHeap))))
			return false;

		g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();

		for (UINT i = 0; i < kBufferCount; ++i)
		{
			if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frames[i].commandAllocator))))
				return false;

			if (FAILED(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_frames[i].backBuffer))))
				return false;

			g_frames[i].rtv = rtvStart;
			g_device->CreateRenderTargetView(g_frames[i].backBuffer, nullptr, g_frames[i].rtv);
			rtvStart.ptr += g_rtvDescriptorSize;
		}
		return true;
	}
}

bool OverlayWindow::IsReady()
{
	return g_initialized && g_overlayHwnd && g_swapChain;
}

bool OverlayWindow::IsOverlaySwapChain(IDXGISwapChain* swapChain)
{
	return swapChain && g_swapChain &&
		static_cast<IDXGISwapChain*>(g_swapChain) == swapChain;
}

void OverlayWindow::EnsureInitialized(ID3D12Device* device,
	ID3D12CommandQueue* queue,
	IDXGISwapChain3* gameSwapChain,
	HWND gameHwnd,
	DXGI_FORMAT format)
{
	if (g_initialized || !device || !queue || !gameSwapChain || !gameHwnd)
		return;

	g_device = device;
	g_device->AddRef();

	if (!CreateOverlayHwnd(gameHwnd))
		return;

	RECT rc{};
	GetClientRect(gameHwnd, &rc);
	const UINT w = rc.right > rc.left ? static_cast<UINT>(rc.right - rc.left) : 1u;
	const UINT h = rc.bottom > rc.top ? static_cast<UINT>(rc.bottom - rc.top) : 1u;

	if (!CreateOverlaySwapChain(queue, gameSwapChain, format, w, h))
		return;
	if (!CreateOverlayRenderTargets(format))
		return;

	g_initialized = true;
	SyncPosition(gameHwnd);
}

void OverlayWindow::SyncPosition(HWND gameHwnd)
{
	if (!g_overlayHwnd || !gameHwnd)
		return;

	RECT rc{};
	GetClientRect(gameHwnd, &rc);
	POINT origin{ 0, 0 };
	ClientToScreen(gameHwnd, &origin);
	const int w = rc.right > rc.left ? (rc.right - rc.left) : 1;
	const int h = rc.bottom > rc.top ? (rc.bottom - rc.top) : 1;

	SetWindowPos(g_overlayHwnd, HWND_TOPMOST, origin.x, origin.y, w, h, SWP_NOACTIVATE | SWP_NOSENDCHANGING);
}

void OverlayWindow::SetVisible(bool visible)
{
	if (!g_overlayHwnd)
		return;
	ShowWindow(g_overlayHwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void OverlayWindow::SetCaptureExcluded(bool excluded)
{
	if (!g_overlayHwnd)
		return;
	SetWindowDisplayAffinity(g_overlayHwnd, excluded ? 0x00000011u /*WDA_EXCLUDEFROMCAPTURE*/ : 0x00000000u);
}

void OverlayWindow::RenderAndPresent(ID3D12GraphicsCommandList* commandList,
	ID3D12CommandQueue* queue,
	ID3D12DescriptorHeap* imguiSrvHeap,
	ID3D12Fence* fence,
	UINT64& fenceCounter,
	HANDLE fenceEvent,
	ImDrawData* drawData)
{
	if (!g_initialized || !commandList || !queue || !drawData || !g_swapChain)
		return;

	const UINT idx = g_swapChain->GetCurrentBackBufferIndex();
	OverlayFrame& frame = g_frames[idx];

	if (fence && fenceEvent && frame.fenceValue != 0 && fence->GetCompletedValue() < frame.fenceValue)
	{
		fence->SetEventOnCompletion(frame.fenceValue, fenceEvent);
		WaitForSingleObject(fenceEvent, 2000);
	}

	frame.commandAllocator->Reset();

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = frame.backBuffer;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

	commandList->Reset(frame.commandAllocator, nullptr);
	commandList->ResourceBarrier(1, &barrier);
	const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	commandList->ClearRenderTargetView(frame.rtv, clearColor, 0, nullptr);
	commandList->OMSetRenderTargets(1, &frame.rtv, FALSE, nullptr);
	if (imguiSrvHeap)
		commandList->SetDescriptorHeaps(1, &imguiSrvHeap);

	ImGui_ImplDX12_RenderDrawData(drawData, commandList);

	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList->ResourceBarrier(1, &barrier);
	commandList->Close();

	queue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(&commandList));

	if (fence)
	{
		const UINT64 signalValue = ++fenceCounter;
		queue->Signal(fence, signalValue);
		frame.fenceValue = signalValue;
	}

	g_swapChain->Present(0, 0);
}

void OverlayWindow::OnGameResize(UINT width, UINT height)
{
	if (!g_initialized || !g_swapChain || width == 0 || height == 0)
		return;

	ReleaseFrameResources();
	g_swapChain->ResizeBuffers(kBufferCount, width, height, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);

	DXGI_SWAP_CHAIN_DESC desc{};
	g_swapChain->GetDesc(&desc);
	CreateOverlayRenderTargets(desc.BufferDesc.Format);
}

void OverlayWindow::Shutdown()
{
	SetCaptureExcluded(false);
	SetVisible(false);

	ReleaseFrameResources();

	if (g_swapChain)
	{
		g_swapChain->Release();
		g_swapChain = nullptr;
	}
	if (g_rtvHeap)
	{
		g_rtvHeap->Release();
		g_rtvHeap = nullptr;
	}
	if (g_overlayHwnd)
	{
		DestroyWindow(g_overlayHwnd);
		g_overlayHwnd = nullptr;
	}
	if (g_device)
	{
		g_device->Release();
		g_device = nullptr;
	}
	g_initialized = false;
}
