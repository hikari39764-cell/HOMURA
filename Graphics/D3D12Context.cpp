#include "D3D12Context.h"

#include <cassert>
#include <format>
#include <limits>
#include <thread>
#include <d3d12sdklayers.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

#include "DebugTools/Logger/Logger.h"
#include "WinApp/WinApp.h"


namespace Homura {

D3D12Context::~D3D12Context() {
	Finalize();
}

bool D3D12Context::Initialize(WinApp* winApp) {
	assert(winApp != nullptr);
	if (winApp == nullptr || winApp->GetHwnd() == nullptr) {
		Log("Failed to initialize DirectX: window is not initialized.\n");
		return false;
	}

	// 前回の初期化途中のResourceも片付けてから、ウィンドウを借りる
	Finalize();
	winApp_ = winApp;

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
	if (!CreateSwapChain()) {
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

	// DepthStencil用のDescriptorHeapを作成する
	if (!CreateDSVDescriptorHeap()) {
		return false;
	}

	// DepthStencil用のResourceを作成する
	if (!CreateDepthStencilResource()) {
		return false;
	}

	// DepthStencilResourceに対してDSVを作成する
	if (!CreateDSV()) {
		return false;
	}

	// GPU処理の完了を待つためのFenceを作成する
	if (!CreateFence()) {
		return false;
	}

	// TextureやImGuiで使うSRV用DescriptorHeapを作成する
	if (!CreateSRVDescriptorHeap()) {
		return false;
	}

	// 画面全体に描画するためのViewportとScissorを設定する
	CreateViewportAndScissor();
	InitializeFixFPS();

	initialized_ = true;
	return true;
}

void D3D12Context::EnableDebugLayer() {
#ifdef _DEBUG
	// DebugLayerを有効にする
	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController;

	HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
	if (SUCCEEDED(hr)) {
		// DirectX12のDebugLayerを有効にする
		debugController->EnableDebugLayer();

		// Debugビルド専用にする
		debugController->SetEnableGPUBasedValidation(TRUE);

		Log("Enable D3D12 DebugLayer!!!\n");
	} else {
		// DebugLayerが使えない環境でも、Releaseビルド同様に起動は続行する
		Log("Failed to enable D3D12 DebugLayer.\n");
	}
#endif
}

void D3D12Context::SetupDebugInfoQueue() {
#ifdef _DEBUG

	// DeviceからInfoQueueを取得する
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;

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
	}
#endif
}

void D3D12Context::Finalize() {
	// GPUがまだResourceを使用している可能性があるので、ComPtrの解放前に待機する
	if (commandQueue_ != nullptr && fence_ != nullptr && fenceEvent_ != nullptr) {
		WaitForGpu();
	}

	if (fenceEvent_ != nullptr) {
		CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
	}

	// 何度呼ばれても同じ状態になるように、所有しているResourceを解放する
	fence_.Reset();
	depthStencilResource_.Reset();
	dsvDescriptorHeap_.Reset();
	srvDescriptorHeap_.Reset();
	rtvDescriptorHeap_.Reset();
	for (UINT i = 0; i < kBackBufferCount; ++i) {
		swapChainResources_[i].Reset();
		rtvHandles_[i] = {};
	}
	swapChain_.Reset();
	commandList_.Reset();
	commandAllocator_.Reset();
	commandQueue_.Reset();
	device_.Reset();
	useAdapter_.Reset();
	dxgiFactory_.Reset();

	backBufferIndex_ = 0;
	rtvDescriptorSize_ = 0;
	srvDescriptorSize_ = 0;
	fenceValue_ = 0;
	viewport_ = {};
	scissorRect_ = {};
	reference_ = {};
	initialized_ = false;
	winApp_ = nullptr;
}

bool D3D12Context::ResetCommandList() {
	if (!initialized_) {
		return false;
	}

	HRESULT hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));

	return SUCCEEDED(hr);
}

bool D3D12Context::ExecuteCommandListAndWait() {
	if (!initialized_) {
		return false;
	}

	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(1, commandLists);
	return WaitForGpu();
}

bool D3D12Context::BeginFrame() {
	if (!initialized_) {
		return false;
	}

	// 今回描画するBackBufferの番号をSwapChainから取得する
	backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

	if (!ResetCommandList()) {
		return false;
	}

	// BackBufferを描画先として使える状態にする
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	commandList_->ResourceBarrier(1, &barrier);

	// DSV用DescriptorHeapの先頭ハンドルを取得する
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
		dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

	// 描画先のRTVとDSVを設定する
	commandList_->OMSetRenderTargets(1, &rtvHandles_[backBufferIndex_], false, &dsvHandle);

	// 描画先を指定した色でクリアする
	const float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
	commandList_->ClearRenderTargetView(rtvHandles_[backBufferIndex_], clearColor, 0, nullptr);

	// 深度値を最も奥の1.0fでクリアする
	commandList_->ClearDepthStencilView(
		dsvHandle,
		D3D12_CLEAR_FLAG_DEPTH,
		1.0f,
		0,
		0,
		nullptr
	);

	// 描画範囲を設定する
	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissorRect_);

	return true;
}

bool D3D12Context::EndFrame() {
	if (!initialized_) {
		return false;
	}

	// BackBufferを画面に表示できる状態にする
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	commandList_->ResourceBarrier(1, &barrier);

	// CommandListへの命令の記録を終える
	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// GPUにCommandListの実行を依頼する
	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(1, commandLists);

	// 描画したBackBufferを画面に表示する
	hr = swapChain_->Present(1, 0);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		Log(std::format("Failed to present BackBuffer: {:#x}\n", static_cast<unsigned long>(hr)));
		return false;
	}

	// 次のフレームでResourceを使い直す前にGPUの完了を待つ
	if (!WaitForGpu()) {
		return false;
	}

	UpdateFixFPS();
	return true;
}

void D3D12Context::SetShaderVisibleDescriptorHeap() {
	if (!initialized_) {
		return;
	}

	// Shaderから参照するDescriptorHeapを設定する
	ID3D12DescriptorHeap* descriptorHeaps[] = {
		srvDescriptorHeap_.Get()
	};
	commandList_->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
}

const Microsoft::WRL::ComPtr<ID3D12Device>& D3D12Context::GetDevice() const {
	return device_;
}

const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& D3D12Context::GetCommandList() const {
	return commandList_;
}

const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& D3D12Context::GetSRVDescriptorHeap() const {
	return srvDescriptorHeap_;
}

bool D3D12Context::CreateFactory() {
	// DXGIファクトリを生成する
	HRESULT hr = ::CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory_));
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	return true;
}

bool D3D12Context::SelectAdapter() {
	// 高性能順にアダプタを列挙して、使用するGPUを決める
	for (UINT i = 0; ; ++i) {
		Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter;
		HRESULT hr = dxgiFactory_->EnumAdapterByGpuPreference(
			i,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(&adapter)
		);
		if (hr == DXGI_ERROR_NOT_FOUND) {
			break;
		}
		if (FAILED(hr)) {
			Log(std::format("Failed to enumerate GPU adapter: {:#x}\n", static_cast<unsigned long>(hr)));
			return false;
		}

		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = adapter->GetDesc3(&adapterDesc);
		if (FAILED(hr)) {
			Log(std::format("Failed to get GPU adapter description: {:#x}\n", static_cast<unsigned long>(hr)));
			return false;
		}

		// ソフトウェアアダプタでなければ採用する
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			useAdapter_ = adapter;
			Log(std::format(L"Use Adapter:{}\n", adapterDesc.Description));
			return true;
		}
	}

	// 有効なアダプタが見つからなければ起動できない
	Log("Failed to find a hardware GPU adapter.\n");
	return false;
}

bool D3D12Context::CreateDevice() {
	// 使用するアダプタ上にD3D12Deviceを生成する
	Microsoft::WRL::ComPtr<ID3D12Device> device;

	// 高い機能レベルから順に試す
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0
	};

	const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };

	HRESULT hr = S_OK;

	for (size_t i = 0; i < _countof(featureLevels); ++i) {
		hr = D3D12CreateDevice(useAdapter_.Get(), featureLevels[i], IID_PPV_ARGS(&device));

		if (SUCCEEDED(hr)) {
			Log(std::format("FeatureLevel : {}\n", featureLevelStrings[i]));
			break;
		}
	}

	// デバイスが生成できなければ起動できない
	assert(device.Get() != nullptr);

	if (device.Get() == nullptr) {
		return false;
	}

	device_ = device;

	Log("Complete create D3D12Device!!!\n");

	SetupDebugInfoQueue();

	return true;
}

bool D3D12Context::CreateCommandQueue() {
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

bool D3D12Context::CreateCommandList() {
	// CommandListが使う命令保存用のAllocatorを作成する
	HRESULT hr = device_->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&commandAllocator_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// CommandListを作成する
	hr = device_->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator_.Get(),
		nullptr,
		IID_PPV_ARGS(&commandList_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// 描画開始時にResetできるように一度閉じる
	hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	Log("Complete create CommandList!!!\n");
	return true;
}

bool D3D12Context::CreateSwapChain() {
	// SwapChainの設定を行う
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = WinApp::kClientWidth;
	swapChainDesc.Height = WinApp::kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = kBackBufferCount;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;

	// CommandQueue、ウィンドウハンドル、設定を渡してSwapChainを作成する
	HRESULT hr = dxgiFactory_->CreateSwapChainForHwnd(
		commandQueue_.Get(),
		winApp_->GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		swapChain1.GetAddressOf()
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// より新しいIDXGISwapChain4として扱えるように変換する
	hr = swapChain1.As(&swapChain_);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	Log("Complete create SwapChain!!!\n");
	return true;
}

bool D3D12Context::CreateRTVDescriptorHeap() {
	// RTV用のDescriptorHeapを作成する
	rtvDescriptorHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kBackBufferCount, false);

	if (rtvDescriptorHeap_.Get() == nullptr) {
		return false;
	}

	// Descriptorの1個分のサイズを取得する
	rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	Log("Complete create RTV DescriptorHeap!!!\n");
	return true;
}

bool D3D12Context::CreateSRVDescriptorHeap() {
	// SRV用のHeapでDescriptorの数は128。Shader内で触るのでShaderVisibleはtrue
	srvDescriptorHeap_ = CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		kSRVDescriptorCount,
		true
	);

	if (srvDescriptorHeap_.Get() == nullptr) {
		return false;
	}

	// Descriptorの1個分のサイズを取得する
	srvDescriptorSize_ =
		device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	Log("Complete create SRV DescriptorHeap!!!\n");
	return true;
}

bool D3D12Context::CreateDSVDescriptorHeap() {
	// DSV用のDescriptorHeapを作成する
	dsvDescriptorHeap_ = CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
		kDSVDescriptorCount,
		false
	);

	if (dsvDescriptorHeap_.Get() == nullptr) {
		return false;
	}

	Log("Complete create DSV DescriptorHeap!!!\n");

	return true;
}

bool D3D12Context::GetSwapChainResources() {
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

bool D3D12Context::CreateDSV() {
	if (depthStencilResource_.Get() == nullptr || dsvDescriptorHeap_.Get() == nullptr) {
		return false;
	}

	// DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	// DSVHeapの先頭にDSVを作る
	device_->CreateDepthStencilView(
		depthStencilResource_.Get(),
		&dsvDesc,
		dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart()
	);

	Log("Complete create DSV!!!\n");

	return true;
}

bool D3D12Context::CreateRTV() {
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
		device_->CreateRenderTargetView(swapChainResources_[i].Get(), &rtvDesc, rtvHandles_[i]);
	}

	Log("Complete create RTV!!!\n");
	return true;
}

bool D3D12Context::CreateDepthStencilResource() {
	// DepthStencilとして使うTextureResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = WinApp::kClientWidth;
	resourceDesc.Height = WinApp::kClientHeight;
	resourceDesc.MipLevels = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	// GPU側のVRAMに配置する
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// 深度値のクリア最適値を設定する
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// DepthStencilResourceを生成する
	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&depthStencilResource_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	Log("Complete create DepthStencilResource!!!\n");

	return true;
}

bool D3D12Context::CreateFence() {
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
	fenceEvent_ = CreateEvent(nullptr, false, false, nullptr);
	assert(fenceEvent_ != nullptr);

	if (fenceEvent_ == nullptr) {
		return false;
	}

	Log("Complete create Fence!!!\n");
	return true;
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> D3D12Context::CreateDescriptorHeap(
	D3D12_DESCRIPTOR_HEAP_TYPE heapType,
	UINT numDescriptors,
	bool shaderVisible
) {
	// DescriptorHeapの作成処理を共通化する
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;

	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescriptors;
	descriptorHeapDesc.Flags =
		shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	HRESULT hr = device_->CreateDescriptorHeap(
		&descriptorHeapDesc,
		IID_PPV_ARGS(&descriptorHeap)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return {};
	}

	return descriptorHeap;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::GetCPUDescriptorHandle(
	const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
	UINT descriptorSize,
	UINT index
) {
	// 指定したDescriptorHeapの指定した位置のCPUHandleを取得する
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU =
		descriptorHeap->GetCPUDescriptorHandleForHeapStart();

	handleCPU.ptr += static_cast<SIZE_T>(descriptorSize) * index;

	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Context::GetGPUDescriptorHandle(
	const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
	UINT descriptorSize,
	UINT index
) {
	// 指定したDescriptorHeapの指定した位置のGPUHandleを取得する
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU =
		descriptorHeap->GetGPUDescriptorHandleForHeapStart();

	handleGPU.ptr += static_cast<SIZE_T>(descriptorSize) * index;

	return handleGPU;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::GetSRVCPUDescriptorHandle(UINT index) const {
	// SRV用DescriptorHeapの指定した位置のCPUHandleを取得する
	return GetCPUDescriptorHandle(
		srvDescriptorHeap_,
		srvDescriptorSize_,
		index
	);
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Context::GetSRVGPUDescriptorHandle(UINT index) const {
	// SRV用DescriptorHeapの指定した位置のGPUHandleを取得する
	return GetGPUDescriptorHandle(
		srvDescriptorHeap_,
		srvDescriptorSize_,
		index
	);
}

void D3D12Context::CreateViewportAndScissor() {
	// Viewportを設定する
	viewport_.Width = static_cast<float>(WinApp::kClientWidth);
	viewport_.Height = static_cast<float>(WinApp::kClientHeight);
	viewport_.TopLeftX = 0.0f;
	viewport_.TopLeftY = 0.0f;
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;

	// Scissorを設定する
	scissorRect_.left = 0;
	scissorRect_.right = WinApp::kClientWidth;
	scissorRect_.top = 0;
	scissorRect_.bottom = WinApp::kClientHeight;

	Log("Complete create Viewport and Scissor!!!\n");
}

void D3D12Context::InitializeFixFPS() {
	// FPS固定用の基準時刻を設定する
	reference_ = std::chrono::steady_clock::now();
}

void D3D12Context::UpdateFixFPS() {
	constexpr std::chrono::microseconds kMinTime(1000000 / 60);
	constexpr std::chrono::microseconds kCheckTime(1000000 / 65);

	// 前のフレームからの経過時間を取得する
	const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - reference_
	);

	// 65FPSより速い時だけ、1フレームが60FPSの時間になるまで待つ
	if (elapsed < kCheckTime) {
		while (std::chrono::steady_clock::now() - reference_ < kMinTime) {
			std::this_thread::sleep_for(std::chrono::microseconds(1));
		}
	}

	// 次のフレームの基準時刻を更新する
	reference_ = std::chrono::steady_clock::now();
}

bool D3D12Context::WaitForGpu() {
	// 初期化前と終了後には、待機するGPU処理がない
	if (commandQueue_ == nullptr) {
		return true;
	}
	if (fence_ == nullptr || fenceEvent_ == nullptr) {
		return false;
	}

	// Fence値を1つ進める
	++fenceValue_;

	const UINT64 waitFenceValue = fenceValue_;

	// CommandQueueにSignalを送る
	// GPUがここまで実行し終わったら、FenceにwaitFenceValueを書き込む
	HRESULT hr = commandQueue_->Signal(fence_.Get(), waitFenceValue);
	if (FAILED(hr)) {
		Log(std::format("Failed to signal GPU fence: {:#x}\n", static_cast<unsigned long>(hr)));
		return false;
	}

	// Deviceが失われた時は完了値が最大値になるので、成功として扱わない
	UINT64 completedValue = fence_->GetCompletedValue();
	if (completedValue == (std::numeric_limits<UINT64>::max)()) {
		Log("Failed to wait for GPU: device was removed.\n");
		return false;
	}

	// GPUがまだ指定したFence値まで到達していなければ待機する
	if (completedValue < waitFenceValue) {
		// 指定したFence値に到達したら、fenceEvent_を通知状態にしてもらう
		hr = fence_->SetEventOnCompletion(waitFenceValue, fenceEvent_);
		if (FAILED(hr)) {
			Log(std::format("Failed to set GPU fence event: {:#x}\n", static_cast<unsigned long>(hr)));
			return false;
		}

		// GPUの処理が終わるまでCPU側を待たせる
		if (WaitForSingleObject(fenceEvent_, INFINITE) != WAIT_OBJECT_0) {
			Log(std::format("Failed to wait for GPU fence: {}\n", GetLastError()));
			return false;
		}

		completedValue = fence_->GetCompletedValue();
		if (completedValue == (std::numeric_limits<UINT64>::max)()) {
			Log("Failed to wait for GPU: device was removed.\n");
			return false;
		}
	}

	return completedValue >= waitFenceValue;
}

} // namespace Homura
