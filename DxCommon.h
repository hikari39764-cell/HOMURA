#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>

class DXCommon {
public:
	bool Initialize();
	void Finalize();

private:
	bool CreateFactory();
	bool SelectAdapter();
	bool CreateDevice();

private:
	IDXGIFactory7* dxgiFactory_ = nullptr;
	IDXGIAdapter4* useAdapter_ = nullptr;
	ID3D12Device* device_ = nullptr;
};