#pragma once

#include <d3d12.h>

#include <string>

#include "externals/DirectXTex/DirectXTex.h"

class TextureManager {
public:
	bool Initialize(
		ID3D12Device* device,
		const std::string& filePath,
		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU,
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU
	);
	void Finalize();

	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureSrvHandleGPU() const;

private:
	DirectX::ScratchImage LoadTexture(const std::string& filePath);
	ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata);
	bool UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages);
	void CreateTextureSRV(
		ID3D12Device* device,
		const DirectX::TexMetadata& metadata,
		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU,
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU
	);

private:
	ID3D12Resource* textureResource_ = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU_ = {};
};
