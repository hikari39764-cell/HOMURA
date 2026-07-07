#include "TextureManager.h"

#include <Windows.h>

#include <cassert>
#include <cstring>
#include <format>

#pragma comment(lib, "windowscodecs.lib")

#include "DebugTools/Logger/Logger.h"

namespace {

	std::wstring ConvertString(const std::string& str) {
		if (str.empty()) {
			return std::wstring();
		}

		int sizeNeeded = MultiByteToWideChar(
			CP_UTF8,
			0,
			str.c_str(),
			-1,
			nullptr,
			0
		);

		if (sizeNeeded == 0) {
			return std::wstring();
		}

		std::wstring result(sizeNeeded, 0);

		MultiByteToWideChar(
			CP_UTF8,
			0,
			str.c_str(),
			-1,
			result.data(),
			sizeNeeded
		);

		if (!result.empty() && result.back() == L'\0') {
			result.pop_back();
		}

		return result;
	}

} // namespace

bool TextureManager::Initialize(
	const Microsoft::WRL::ComPtr<ID3D12Device>& device,
	const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList,
	const std::string& filePath,
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU,
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU,
	Microsoft::WRL::ComPtr<ID3D12Resource>* intermediateResource
) {
	assert(device.Get() != nullptr);
	assert(commandList.Get() != nullptr);
	assert(intermediateResource != nullptr);

	if (device.Get() == nullptr || commandList.Get() == nullptr || intermediateResource == nullptr) {
		return false;
	}

	intermediateResource->Reset();

	// Textureファイルを読み込んでmipmap込みの画像データにする
	DirectX::ScratchImage mipImages = LoadTexture(filePath);

	if (mipImages.GetImageCount() == 0) {
		return false;
	}

	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

	// 読み込んだ画像情報を基にGPU側のTextureResourceを作る
	textureResource_ = CreateTextureResource(device, metadata);

	if (textureResource_.Get() == nullptr) {
		return false;
	}

	// CPUから書き込む中間Resourceを経由してTextureResourceへ転送する
	*intermediateResource = UploadTextureData(device, commandList, textureResource_, mipImages);

	if (intermediateResource->Get() == nullptr) {
		return false;
	}

	// PixelShaderからTextureを読めるようにSRVを作る
	CreateTextureSRV(device, metadata, textureSrvHandleCPU, textureSrvHandleGPU);

	Log(std::format("Complete create TextureManager, path:{}!!!\n", filePath));
	return true;
}

void TextureManager::Finalize() {
	textureResource_.Reset();
	textureSrvHandleGPU_ = {};
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetTextureSrvHandleGPU() const {
	return textureSrvHandleGPU_;
}

DirectX::ScratchImage TextureManager::LoadTexture(const std::string& filePath) {
	// Textureファイルを読んでプログラムで扱えるようにする
	DirectX::ScratchImage image{};
	const std::wstring filePathW = ConvertString(filePath);

	HRESULT hr = DirectX::LoadFromWICFile(
		filePathW.c_str(),
		DirectX::WIC_FLAGS_FORCE_SRGB,
		nullptr,
		image
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		Log(std::format("Failed to load texture, path:{}\n", filePath));
		return {};
	}

	// mipmapを作成する
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(
		image.GetImages(),
		image.GetImageCount(),
		image.GetMetadata(),
		DirectX::TEX_FILTER_SRGB,
		0,
		mipImages
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		Log(std::format("Failed to generate mipmaps, path:{}\n", filePath));
		return {};
	}

	// mipmap付きのデータを返す
	return mipImages;
}

Microsoft::WRL::ComPtr<ID3D12Resource> TextureManager::CreateTextureResource(
	const Microsoft::WRL::ComPtr<ID3D12Device>& device,
	const DirectX::TexMetadata& metadata
) {
	// metadataを基にResourceの設定を行う
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width);
	resourceDesc.Height = UINT(metadata.height);
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);
	resourceDesc.DepthOrArraySize = UINT16(
		metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE3D
		? metadata.depth
		: metadata.arraySize
	);
	resourceDesc.Format = metadata.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

	// TextureはGPU側のVRAMに配置する
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// データ転送を受けられる状態でTexture用Resourceを生成する
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&resource)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return {};
	}

	return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> TextureManager::CreateIntermediateResource(
	const Microsoft::WRL::ComPtr<ID3D12Device>& device,
	uint64_t sizeInBytes
) {
	// CPUから書き込むため、UploadHeap上に中間Resourceを作る
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeInBytes;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resource)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return {};
	}

	return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> TextureManager::UploadTextureData(
	const Microsoft::WRL::ComPtr<ID3D12Device>& device,
	const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList,
	const Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
	const DirectX::ScratchImage& mipImages
) {
	const size_t imageCount = mipImages.GetImageCount();

	if (imageCount == 0) {
		return {};
	}

	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	subresources.reserve(imageCount);
	const DirectX::Image* images = mipImages.GetImages();

	for (size_t index = 0; index < imageCount; ++index) {
		const DirectX::Image& image = images[index];
		assert(image.pixels != nullptr);

		if (image.pixels == nullptr) {
			return {};
		}

		D3D12_SUBRESOURCE_DATA subresource{};
		subresource.pData = image.pixels;
		subresource.RowPitch = LONG_PTR(image.rowPitch);
		subresource.SlicePitch = LONG_PTR(image.slicePitch);
		subresources.push_back(subresource);
	}

	const UINT subresourceCount = UINT(subresources.size());
	const D3D12_RESOURCE_DESC resourceDesc = texture->GetDesc();

	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subresourceCount);
	std::vector<UINT> numRows(subresourceCount);
	std::vector<uint64_t> rowSizesInBytes(subresourceCount);
	uint64_t intermediateSize = 0;

	// Textureへ転送するために必要な中間Resourceのサイズを計算する
	device->GetCopyableFootprints(
		&resourceDesc,
		0,
		subresourceCount,
		0,
		layouts.data(),
		numRows.data(),
		rowSizesInBytes.data(),
		&intermediateSize
	);

	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource =
		CreateIntermediateResource(device, intermediateSize);

	if (intermediateResource.Get() == nullptr) {
		return {};
	}

	if (!CopyTextureDataToIntermediate(
		intermediateResource,
		subresources,
		layouts,
		numRows,
		rowSizesInBytes
	)) {
		return {};
	}

	// 中間ResourceからTextureResourceへコピーする命令を積む
	for (UINT index = 0; index < subresourceCount; ++index) {
		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource = texture.Get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		destination.SubresourceIndex = index;

		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = intermediateResource.Get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		source.PlacedFootprint = layouts[index];

		commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
	}

	// 転送後にShaderから読める状態へ変更する
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList->ResourceBarrier(1, &barrier);

	return intermediateResource;
}

bool TextureManager::CopyTextureDataToIntermediate(
	const Microsoft::WRL::ComPtr<ID3D12Resource>& intermediateResource,
	const std::vector<D3D12_SUBRESOURCE_DATA>& subresources,
	const std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT>& layouts,
	const std::vector<UINT>& numRows,
	const std::vector<uint64_t>& rowSizesInBytes
) {
	assert(intermediateResource.Get() != nullptr);

	if (intermediateResource.Get() == nullptr) {
		return false;
	}

	// 中間ResourceへCPUから実データを書き込む
	uint8_t* mappedData = nullptr;
	HRESULT hr = intermediateResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	for (size_t index = 0; index < subresources.size(); ++index) {
		const D3D12_SUBRESOURCE_DATA& subresource = subresources[index];
		const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout = layouts[index];
		const uint8_t* sourceBase = static_cast<const uint8_t*>(subresource.pData);
		uint8_t* destinationBase = mappedData + layout.Offset;

		for (UINT z = 0; z < layout.Footprint.Depth; ++z) {
			const uint8_t* sourceSlice =
				sourceBase + static_cast<size_t>(subresource.SlicePitch) * z;
			uint8_t* destinationSlice =
				destinationBase + static_cast<size_t>(layout.Footprint.RowPitch) * numRows[index] * z;

			for (UINT y = 0; y < numRows[index]; ++y) {
				std::memcpy(
					destinationSlice + static_cast<size_t>(layout.Footprint.RowPitch) * y,
					sourceSlice + static_cast<size_t>(subresource.RowPitch) * y,
					static_cast<size_t>(rowSizesInBytes[index])
				);
			}
		}
	}

	intermediateResource->Unmap(0, nullptr);
	return true;
}

void TextureManager::CreateTextureSRV(
	const Microsoft::WRL::ComPtr<ID3D12Device>& device,
	const DirectX::TexMetadata& metadata,
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU,
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU
) {
	// metadataを基にSRVの設定を行う
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	textureSrvHandleGPU_ = textureSrvHandleGPU;

	// SRVを作成する
	device->CreateShaderResourceView(textureResource_.Get(), &srvDesc, textureSrvHandleCPU);
}
