#include "TextureManager.h"

#include <Windows.h>

#include <cassert>
#include <format>

#pragma comment(lib, "windowscodecs.lib")

#include "Logger.h"

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

bool TextureManager::Initialize(ID3D12Device* device, const std::string& filePath) {
	assert(device != nullptr);

	if (device == nullptr) {
		return false;
	}

	if (!CreateSRVDescriptorHeap(device)) {
		return false;
	}

	// Textureを読み込んでmipmap込みの画像データにする
	DirectX::ScratchImage mipImages = LoadTexture(filePath);

	if (mipImages.GetImageCount() == 0) {
		return false;
	}

	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

	// 読み込んだ画像情報を基にTexture用Resourceを作成する
	textureResource_ = CreateTextureResource(device, metadata);

	if (textureResource_ == nullptr) {
		return false;
	}

	// Texture用Resourceへ実際の画像データを転送する
	if (!UploadTextureData(textureResource_, mipImages)) {
		return false;
	}

	// PixelShaderからTextureを読めるようにSRVを作成する
	CreateTextureSRV(device, metadata);

	Log(std::format("Complete create TextureManager, path:{}!!!\n", filePath));
	return true;
}

void TextureManager::Finalize() {
	if (textureResource_ != nullptr) {
		textureResource_->Release();
		textureResource_ = nullptr;
	}

	if (srvDescriptorHeap_ != nullptr) {
		srvDescriptorHeap_->Release();
		srvDescriptorHeap_ = nullptr;
	}

	textureSrvHandleGPU_ = {};
	srvDescriptorSize_ = 0;
}

ID3D12DescriptorHeap* TextureManager::GetSRVDescriptorHeap() const {
	return srvDescriptorHeap_;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetTextureSrvHandleGPU() const {
	return textureSrvHandleGPU_;
}

bool TextureManager::CreateSRVDescriptorHeap(ID3D12Device* device) {
	// Shaderから参照できるSRV用DescriptorHeapを作成する
	D3D12_DESCRIPTOR_HEAP_DESC srvDescriptorHeapDesc{};
	srvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvDescriptorHeapDesc.NumDescriptors = kSRVDescriptorCount;
	srvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	HRESULT hr = device->CreateDescriptorHeap(
		&srvDescriptorHeapDesc,
		IID_PPV_ARGS(&srvDescriptorHeap_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// Descriptorの1個分のサイズを取得する
	srvDescriptorSize_ =
		device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	return true;
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

ID3D12Resource* TextureManager::CreateTextureResource(
	ID3D12Device* device,
	const DirectX::TexMetadata& metadata
) {
	// metadataを基にResourceの設定を行う
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width);
	resourceDesc.Height = UINT(metadata.height);
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
	resourceDesc.Format = metadata.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

	// TextureにCPUから書き込めるようにHeapを設定する
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_CUSTOM;
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

	// Texture用Resourceを生成する
	ID3D12Resource* resource = nullptr;
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
		return nullptr;
	}

	return resource;
}

bool TextureManager::UploadTextureData(
	ID3D12Resource* texture,
	const DirectX::ScratchImage& mipImages
) {
	// 全てのmipmapについてTextureへデータを転送する
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

	for (size_t mipLevel = 0; mipLevel < metadata.mipLevels; ++mipLevel) {
		// mipmapレベルを指定して画像を取得する
		const DirectX::Image* img = mipImages.GetImage(mipLevel, 0, 0);
		assert(img != nullptr);

		if (img == nullptr) {
			return false;
		}

		// Textureに転送する
		HRESULT hr = texture->WriteToSubresource(
			UINT(mipLevel),
			nullptr,
			img->pixels,
			UINT(img->rowPitch),
			UINT(img->slicePitch)
		);
		assert(SUCCEEDED(hr));

		if (FAILED(hr)) {
			return false;
		}
	}

	return true;
}

void TextureManager::CreateTextureSRV(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
	// metadataを基にSRVの設定を行う
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	// SRVを作成するDescriptorHeapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU =
		srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	textureSrvHandleCPU.ptr += static_cast<SIZE_T>(srvDescriptorSize_) * kTextureSRVIndex;

	textureSrvHandleGPU_ = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
	textureSrvHandleGPU_.ptr += static_cast<SIZE_T>(srvDescriptorSize_) * kTextureSRVIndex;

	// SRVを作成する
	device->CreateShaderResourceView(textureResource_, &srvDesc, textureSrvHandleCPU);
}
