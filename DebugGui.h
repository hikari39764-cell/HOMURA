#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgiformat.h>

class DebugGui {
public:
	bool Initialize(
		HWND hwnd,
		ID3D12Device* device,
		int bufferCount,
		DXGI_FORMAT rtvFormat,
		ID3D12DescriptorHeap* srvDescriptorHeap,
		D3D12_CPU_DESCRIPTOR_HANDLE fontSrvHandleCPU,
		D3D12_GPU_DESCRIPTOR_HANDLE fontSrvHandleGPU
	);
	void Finalize();

	void BeginFrame();
	void ShowDemoWindow();
	void EndFrame();
	void Render(ID3D12GraphicsCommandList* commandList);

private:
	bool initialized_ = false;
};
