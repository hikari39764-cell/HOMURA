#pragma once

#include <d3d12.h>

#include <cstdint>
#include <string>
#include <vector>

#include "externals/DirectXTex/DirectXTex.h"

class TextureManager {
public:
	bool Initialize(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* commandList,
		const std::string& filePath,
		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU,
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU,
		ID3D12Resource** intermediateResource
	);
	void Finalize();

	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureSrvHandleGPU() const;

private:
	DirectX::ScratchImage LoadTexture(const std::string& filePath);
	ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata);
	ID3D12Resource* CreateIntermediateResource(ID3D12Device* device, uint64_t sizeInBytes);
	[[nodiscard]]
	ID3D12Resource* UploadTextureData(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* commandList,
		ID3D12Resource* texture,
		const DirectX::ScratchImage& mipImages
	);
	bool CopyTextureDataToIntermediate(
		ID3D12Resource* intermediateResource,
		const std::vector<D3D12_SUBRESOURCE_DATA>& subresources,
		const std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT>& layouts,
		const std::vector<UINT>& numRows,
		const std::vector<uint64_t>& rowSizesInBytes
	);
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
