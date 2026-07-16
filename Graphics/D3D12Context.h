#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

namespace Homura {

class D3D12Context {
public:
	bool Initialize(HWND hwnd);
	void Finalize();

	bool ResetCommandList();
	bool ExecuteCommandListAndWait();

	bool BeginFrame();
	bool EndFrame();
	void SetShaderVisibleDescriptorHeap();

	const Microsoft::WRL::ComPtr<ID3D12Device>& GetDevice() const;
	const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& GetCommandList() const;
	const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& GetSRVDescriptorHeap() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUDescriptorHandle(UINT index) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(UINT index) const;

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
	bool CreateSRVDescriptorHeap();
	bool CreateDSVDescriptorHeap();
	bool GetSwapChainResources();
	bool CreateRTV();
	bool CreateDepthStencilResource();
	bool CreateDSV();
	bool CreateFence();

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE heapType,
		UINT numDescriptors,
		bool shaderVisible
	);
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
		const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
		UINT descriptorSize,
		UINT index
	) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
		const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
		UINT descriptorSize,
		UINT index
	) const;
	void CreateViewportAndScissor();

	void WaitForGpu();

private:
	static constexpr UINT kBackBufferCount = 2;
	static constexpr UINT kSRVDescriptorCount = 128;
	static constexpr UINT kDSVDescriptorCount = 1;

private:
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter_;
	Microsoft::WRL::ComPtr<ID3D12Device> device_;

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;

	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources_[kBackBufferCount];
	UINT backBufferIndex_ = 0;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[kBackBufferCount] = {};
	UINT rtvDescriptorSize_ = 0;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
	UINT srvDescriptorSize_ = 0;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;

	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
	UINT64 fenceValue_ = 0;
	HANDLE fenceEvent_ = nullptr;

	D3D12_VIEWPORT viewport_ = {};
	D3D12_RECT scissorRect_ = {};
};

} // namespace Homura
