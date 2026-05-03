#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

class DXCommon {
public:
	bool Initialize(HWND hwnd);
	void Finalize();

	void Draw();

private:
	static constexpr UINT kBackBufferCount = 2;

private:
	void EnableDebugLayer();
	void SetupDebugInfoQueue();

	bool CreateFactory();
	bool SelectAdapter();
	bool CreateDevice();
	bool CreateCommandQueue();
	bool CreateCommandList();
	bool CreateSwapChain(HWND hwnd);
	bool CreateRTVDescriptorHeap();
	bool GetSwapChainResources();
	bool CreateRTV();
	bool CreateFence();

	void WaitForGpu();

private:
	IDXGIFactory7* dxgiFactory_ = nullptr;
	IDXGIAdapter4* useAdapter_ = nullptr;
	ID3D12Device* device_ = nullptr;

	ID3D12CommandQueue* commandQueue_ = nullptr;
	ID3D12CommandAllocator* commandAllocator_ = nullptr;
	ID3D12GraphicsCommandList* commandList_ = nullptr;

	IDXGISwapChain4* swapChain_ = nullptr;
	ID3D12Resource* swapChainResources_[kBackBufferCount] = {};

	ID3D12DescriptorHeap* rtvDescriptorHeap_ = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[kBackBufferCount] = {};
	UINT rtvDescriptorSize_ = 0;

	ID3D12Fence* fence_ = nullptr;
	UINT64 fenceValue_ = 0;
	HANDLE fenceEvent_ = nullptr;
};