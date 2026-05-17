#pragma once

#include <d3d12.h>

#include <string>

#include "externals/DirectXTex/DirectXTex.h"

class TextureManager {
public:
	bool Initialize(ID3D12Device* device, const std::string& filePath);
	void Finalize();

	ID3D12DescriptorHeap* GetSRVDescriptorHeap() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureSrvHandleGPU() const;

private:
	static constexpr UINT kSRVDescriptorCount = 2;
	static constexpr UINT kTextureSRVIndex = 1;

private:
	bool CreateSRVDescriptorHeap(ID3D12Device* device);
	DirectX::ScratchImage LoadTexture(const std::string& filePath);
	ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata);
	bool UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages);
	void CreateTextureSRV(ID3D12Device* device, const DirectX::TexMetadata& metadata);

private:
	ID3D12DescriptorHeap* srvDescriptorHeap_ = nullptr;
	ID3D12Resource* textureResource_ = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU_ = {};
	UINT srvDescriptorSize_ = 0;
};
