#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <string>
#include <vector>

#include "externals/DirectXTex/DirectXTex.h"

namespace Homura {

class TextureManager {
public:
	bool Initialize(
		const Microsoft::WRL::ComPtr<ID3D12Device>& device,
		const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList,
		const std::string& filePath,
		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU,
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU,
		Microsoft::WRL::ComPtr<ID3D12Resource>* intermediateResource
	);
	void Finalize();

	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureSrvHandleGPU() const;

private:
	DirectX::ScratchImage LoadTexture(const std::string& filePath);
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(
		const Microsoft::WRL::ComPtr<ID3D12Device>& device,
		const DirectX::TexMetadata& metadata
	);
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateIntermediateResource(
		const Microsoft::WRL::ComPtr<ID3D12Device>& device,
		uint64_t sizeInBytes
	);
	[[nodiscard]]
	Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(
		const Microsoft::WRL::ComPtr<ID3D12Device>& device,
		const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList,
		const Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
		const DirectX::ScratchImage& mipImages
	);
	bool CopyTextureDataToIntermediate(
		const Microsoft::WRL::ComPtr<ID3D12Resource>& intermediateResource,
		const std::vector<D3D12_SUBRESOURCE_DATA>& subresources,
		const std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT>& layouts,
		const std::vector<UINT>& numRows,
		const std::vector<uint64_t>& rowSizesInBytes
	);
	void CreateTextureSRV(
		const Microsoft::WRL::ComPtr<ID3D12Device>& device,
		const DirectX::TexMetadata& metadata,
		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU,
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU
	);

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource_;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU_ = {};
};

} // namespace Homura
