#include "DxCommon.h"

#include <cassert>
#include <format>
#include <d3d12sdklayers.h>
#include <dxgidebug.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

#include "Logger.h"
#include "WinConfig.h"


bool DXCommon::Initialize(HWND hwnd) {
	// DebugLayerはD3D12Deviceを作る前に有効化
	EnableDebugLayer();

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

	// CPUからGPUへ命令を投げるためのキューを作成する
	if (!CreateCommandQueue()) {
		return false;
	}

	// GPUへ渡す命令を積み込むためのCommandListを作成する
	if (!CreateCommandList()) {
		return false;
	}

	// 画面表示用のSwapChainを作成する
	if (!CreateSwapChain(hwnd)) {
		return false;
	}

	// BackBufferへ描画するためのRTV用DescriptorHeapを作成する
	if (!CreateRTVDescriptorHeap()) {
		return false;
	}

	// SwapChainが持っているBackBufferのResourceを取得する
	if (!GetSwapChainResources()) {
		return false;
	}

	// 取得したBackBufferに対してRTVを作成する
	if (!CreateRTV()) {
		return false;
	}

	// GPU処理の完了を待つためのFenceを作成する
	if (!CreateFence()) {
		return false;
	}

	return true;
}

void DXCommon::EnableDebugLayer() {
#ifdef _DEBUG
	// DebugLayerを有効にする
	ID3D12Debug1* debugController = nullptr;

	HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
	if (SUCCEEDED(hr)) {
		// DirectX12のDebugLayerを有効にする
		debugController->EnableDebugLayer();

		// Debugビルド専用にする
		debugController->SetEnableGPUBasedValidation(TRUE);

		Log("Enable D3D12 DebugLayer!!!\n");

		debugController->Release();
		debugController = nullptr;
	} else {
		// DebugLayerが使えない環境でも、Releaseビルド同様に起動は続行する
		Log("Failed to enable D3D12 DebugLayer.\n");
	}
#endif
}

void DXCommon::SetupDebugInfoQueue() {
#ifdef _DEBUG

	// DeviceからInfoQueueを取得する
	ID3D12InfoQueue* infoQueue = nullptr;

	HRESULT hr = device_->QueryInterface(IID_PPV_ARGS(&infoQueue));
	if (SUCCEEDED(hr)) {
		// 深刻な破損エラーが出たら停止する
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);

		// 通常のエラーが出たら停止する
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

		// 警告が出たら停止する
		// infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		// 抑制するメッセージID
		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};

		// 抑制するメッセージレベル
		D3D12_MESSAGE_SEVERITY severities[] = {
			D3D12_MESSAGE_SEVERITY_INFO
		};

		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = static_cast<UINT>(_countof(denyIds));
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = static_cast<UINT>(_countof(severities));
		filter.DenyList.pSeverityList = severities;

		// 指定したメッセージをInfoQueueに保存しないようにする
		infoQueue->PushStorageFilter(&filter);

		Log("Setup D3D12 InfoQueue!!!\n");

		infoQueue->Release();
		infoQueue = nullptr;
	}
#endif
}

void DXCommon::ReportLiveObjects() {
#ifdef _DEBUG
	// DirectX12 / DXGIの解放漏れを確認する
	// すべてのDirectXオブジェクトをReleaseした後に呼び出すこと
	IDXGIDebug1* debug = nullptr;

	HRESULT hr = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug));
	if (SUCCEEDED(hr)) {
		Log("========== DXGI Live Object Report Start!!!!!!!!!!!!!!!!!!!!!!!!!! ==========\n");

		// DXGI全体の生存オブジェクトを出力する
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);

		// アプリケーション側で作成したDXGI関連オブジェクトを出力する
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);

		// D3D12関連の生存オブジェクトを出力する
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);

		Log("========== DXGI Live Object Report End !!!!!!!!!!!!!!!!!!!!!==========\n");

		debug->Release();
		debug = nullptr;
	} else {
		Log("Failed to get DXGI Debug Interface.\n");
	}
#endif
}

void DXCommon::Finalize() {
	// GPUがまだResourceを使用している可能性があるので、解放前に待機する
	if (commandQueue_ != nullptr && fence_ != nullptr) {
		WaitForGpu();
	}

	if (fenceEvent_ != nullptr) {
		CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
	}

	if (fence_ != nullptr) {
		fence_->Release();
		fence_ = nullptr;
	}

	if (rtvDescriptorHeap_ != nullptr) {
		rtvDescriptorHeap_->Release();
		rtvDescriptorHeap_ = nullptr;
	}

	for (UINT i = 0; i < kBackBufferCount; ++i) {
		if (swapChainResources_[i] != nullptr) {
			swapChainResources_[i]->Release();
			swapChainResources_[i] = nullptr;
		}
	}

	if (swapChain_ != nullptr) {
		swapChain_->Release();
		swapChain_ = nullptr;
	}

	if (commandList_ != nullptr) {
		commandList_->Release();
		commandList_ = nullptr;
	}

	if (commandAllocator_ != nullptr) {
		commandAllocator_->Release();
		commandAllocator_ = nullptr;
	}

	if (commandQueue_ != nullptr) {
		commandQueue_->Release();
		commandQueue_ = nullptr;
	}

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

	// すべてのDirectX関連オブジェクトを解放した後、解放漏れがないか確認する
	ReportLiveObjects();
}

void DXCommon::Draw() {
	// 今から書き込むBackBufferの番号をSwapChainから取得する
	UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

	// 前フレームの命令を保存していたAllocatorを再利用できる状態に戻す
	HRESULT hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));

	// CommandListも再び命令を書き込める状態に戻す
	hr = commandList_->Reset(commandAllocator_, nullptr);
	assert(SUCCEEDED(hr));

	// BackBufferを描画先として使える状態へ変更する
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources_[backBufferIndex];
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	commandList_->ResourceBarrier(1, &barrier);

	// これから描画するRTVを設定する
	commandList_->OMSetRenderTargets(1, &rtvHandles_[backBufferIndex], false, nullptr);

	// 指定した色で画面全体をクリアする
	const float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
	commandList_->ClearRenderTargetView(rtvHandles_[backBufferIndex], clearColor, 0, nullptr);

	// BackBufferを表示用の状態へ戻す
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

	commandList_->ResourceBarrier(1, &barrier);

	// CommandListの内容を確定する
	hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	// GPUにCommandListの実行を依頼する
	ID3D12CommandList* commandLists[] = { commandList_ };
	commandQueue_->ExecuteCommandLists(1, commandLists);

	// 画面を交換して、今描いたBackBufferを表示する
	hr = swapChain_->Present(1, 0);
	assert(SUCCEEDED(hr));

	// 毎フレームGPUの完了を待つ
	WaitForGpu();
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

	SetupDebugInfoQueue();

	return true;
}

bool DXCommon::CreateCommandQueue() {
	// CommandQueueを作成する
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	commandQueueDesc.NodeMask = 0;

	HRESULT hr = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	Log("Complete create CommandQueue!!!\n");
	return true;
}

bool DXCommon::CreateCommandList() {
	// CommandListが使う命令保存用のAllocatorを作成する
	HRESULT hr = device_->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&commandAllocator_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	//　CommandListを作成する
	hr = device_->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator_,
		nullptr,
		IID_PPV_ARGS(&commandList_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// 一度閉じて
	hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	Log("Complete create CommandList!!!\n");
	return true;
}

bool DXCommon::CreateSwapChain(HWND hwnd) {
	// SwapChainの設定を行う
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = WinConfig::kClientWidth;
	swapChainDesc.Height = WinConfig::kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = kBackBufferCount;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	IDXGISwapChain1* swapChain1 = nullptr;

	// CommandQueue、ウィンドウハンドル、設定を渡してSwapChainを作成する
	HRESULT hr = dxgiFactory_->CreateSwapChainForHwnd(
		commandQueue_,
		hwnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain1
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// より新しいIDXGISwapChain4として扱えるように変換する
	hr = swapChain1->QueryInterface(IID_PPV_ARGS(&swapChain_));
	assert(SUCCEEDED(hr));

	swapChain1->Release();
	swapChain1 = nullptr;

	if (FAILED(hr)) {
		return false;
	}

	Log("Complete create SwapChain!!!\n");
	return true;
}

bool DXCommon::CreateRTVDescriptorHeap() {
	// RTV用のDescriptorHeapを作成する
	D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc{};
	rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDescriptorHeapDesc.NumDescriptors = kBackBufferCount;
	rtvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvDescriptorHeapDesc.NodeMask = 0;

	HRESULT hr = device_->CreateDescriptorHeap(
		&rtvDescriptorHeapDesc,
		IID_PPV_ARGS(&rtvDescriptorHeap_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// Descriptorの1個分のサイズを取得する
	rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	Log("Complete create RTV DescriptorHeap!!!\n");
	return true;
}

bool DXCommon::GetSwapChainResources() {
	// SwapChainが持っているBackBufferのResourceを取得する
	for (UINT i = 0; i < kBackBufferCount; ++i) {
		HRESULT hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&swapChainResources_[i]));
		assert(SUCCEEDED(hr));

		if (FAILED(hr)) {
			return false;
		}
	}

	Log("Complete get SwapChain Resources!!!\n");
	return true;
}

bool DXCommon::CreateRTV() {
	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	// DescriptorHeapの先頭ハンドルを取得する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle =
		rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

	for (UINT i = 0; i < kBackBufferCount; ++i) {
		// i番目のRTVを書き込む位置を決める
		rtvHandles_[i] = rtvStartHandle;
		rtvHandles_[i].ptr += static_cast<SIZE_T>(rtvDescriptorSize_) * i;

		// BackBufferのResourceに対してRTVを作成する
		device_->CreateRenderTargetView(swapChainResources_[i], &rtvDesc, rtvHandles_[i]);
	}

	Log("Complete create RTV!!!\n");
	return true;
}

bool DXCommon::CreateFence() {
	// GPUの処理完了をCPU側で確認するためのFenceを作成する
	fenceValue_ = 0;

	HRESULT hr = device_->CreateFence(
		fenceValue_,
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&fence_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// Fenceの完了通知を受け取るためのWindowsイベントを作成する
	// GPUの処理が終わるまでCPUを待機させるときに使う
	fenceEvent_ = CreateEvent(nullptr, false, false, nullptr);
	assert(fenceEvent_ != nullptr);

	if (fenceEvent_ == nullptr) {
		return false;
	}

	Log("Complete create Fence!!!\n");
	return true;
}

void DXCommon::WaitForGpu() {
	// Fence値を1つ進める
	++fenceValue_;

	const UINT64 waitFenceValue = fenceValue_;

	// CommandQueueにSignalを送る
	// GPUがここまで実行し終わったら、FenceにwaitFenceValueを書き込む
	HRESULT hr = commandQueue_->Signal(fence_, waitFenceValue);
	assert(SUCCEEDED(hr));

	// GPUがまだ指定したFence値まで到達していなければ待機する
	if (fence_->GetCompletedValue() < waitFenceValue) {
		// 指定したFence値に到達したら、fenceEvent_を通知状態にしてもらう
		hr = fence_->SetEventOnCompletion(waitFenceValue, fenceEvent_);
		assert(SUCCEEDED(hr));

		// GPUの処理が終わるまでCPU側を待たせる
		WaitForSingleObject(fenceEvent_, INFINITE);
	}
}