#include "DXCommon.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <format>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#include "Logger.h"

bool DXCommon::Initialize() {
	// DX12を使うための基本初期化
	if (!CreateFactory()) {
		return false;
	}

	if (!SelectAdapter()) {
		return false;
	}

	if (!CreateDevice()) {
		return false;
	}

	return true;
}

void DXCommon::Finalize() {
	// 生成した順とは逆順で解放する
	if (device_ != nullptr) {
		device_->Release();
		device_ = nullptr;
	}

	if (useAdapter_ != nullptr) {
		useAdapter_->Release();
		useAdapter_ = nullptr;
	}

	if (dxgiFactory_ != nullptr) {
		dxgiFactory_->Release();
		dxgiFactory_ = nullptr;
	}
}

bool DXCommon::CreateFactory() {
	// DXGIファクトリを生成する
	HRESULT hr = ::CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory_));
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	return true;
}

bool DXCommon::SelectAdapter() {
	// 高性能順にアダプタを列挙して、使用するGPUを決める
	for (UINT i = 0;
		dxgiFactory_->EnumAdapterByGpuPreference(
			i,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(&useAdapter_)) != DXGI_ERROR_NOT_FOUND;
			++i) {

		DXGI_ADAPTER_DESC3 adapterDesc{};
		HRESULT hr = useAdapter_->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));

		// ソフトウェアアダプタでなければ採用する
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			Log(std::format(L"Use Adapter:{}\n", adapterDesc.Description));
			break;
		}

		// ソフトウェアアダプタなら候補から外す
		useAdapter_->Release();
		useAdapter_ = nullptr;
	}

	// 有効なアダプタが見つからなければ起動できない
	assert(useAdapter_ != nullptr);

	if (useAdapter_ == nullptr) {
		return false;
	}

	return true;
}

bool DXCommon::CreateDevice() {
	// 使用するアダプタ上にD3D12Deviceを生成する
	ID3D12Device* device = nullptr;

	// 高い機能レベルから順に試す
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0
	};

	const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };

	HRESULT hr = S_OK;

	for (size_t i = 0; i < _countof(featureLevels); ++i) {
		hr = D3D12CreateDevice(useAdapter_, featureLevels[i], IID_PPV_ARGS(&device));

		if (SUCCEEDED(hr)) {
			Log(std::format("FeatureLevel : {}\n", featureLevelStrings[i]));
			break;
		}
	}

	// デバイスが生成できなければ起動できない
	assert(device != nullptr);

	if (device == nullptr) {
		return false;
	}

	device_ = device;

	Log("Complete create D3D12Device!!!\n");
	return true;
}