#include "D3D12Context.h"

#include <cassert>
#include <format>
#include <d3d12sdklayers.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

#include "DebugTools/Logger/Logger.h"
#include "WinApp/WinConfig.h"


namespace Homura {

bool D3D12Context::Initialize(HWND hwnd) {
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
	if (commandQueue_.Get() != nullptr && fence_.Get() != nullptr) {
		WaitForGpu();
	}


	if (fenceEvent_ != nullptr) {
		CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
	}
}

bool D3D12Context::ResetCommandList() {
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
	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(1, commandLists);
	WaitForGpu();

	return true;
}

bool D3D12Context::BeginFrame() {
	// ???????BackBuffer????SwapChain??????
	backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

	if (!ResetCommandList()) {
		return false;
	}

	// BackBuffer?????????????????
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	commandList_->ResourceBarrier(1, &barrier);

	// DSV?DescriptorHeap????????????
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
		dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

	// ????????RTV?DSV?????
	commandList_->OMSetRenderTargets(1, &rtvHandles_[backBufferIndex_], false, &dsvHandle);

	// ????????????????
	const float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
	commandList_->ClearRenderTargetView(rtvHandles_[backBufferIndex_], clearColor, 0, nullptr);

	// ????????1.0f??????
	commandList_->ClearDepthStencilView(
		dsvHandle,
		D3D12_CLEAR_FLAG_DEPTH,
		1.0f,
		0,
		0,
		nullptr
	);

	// ?????????
	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissorRect_);

	return true;
}

bool D3D12Context::EndFrame() {
	// BackBuffer??????????
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	commandList_->ResourceBarrier(1, &barrier);

	// CommandList????????
	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// GPU?CommandList????????
	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(1, commandLists);

	// ????????????BackBuffer?????
	hr = swapChain_->Present(1, 0);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// ?????GPU??????
	WaitForGpu();
	return true;
}

void D3D12Context::SetShaderVisibleDescriptorHeap() {
	// Shader??????DescriptorHeap?????
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
		useAdapter_.Reset();
	}

	// 有効なアダプタが見つからなければ起動できない
	assert(useAdapter_ != nullptr);

	if (useAdapter_ == nullptr) {
		return false;
	}

	return true;
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

	//　CommandListを作成する
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

	// 一度閉じて
	hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	Log("Complete create CommandList!!!\n");
	return true;
}

bool D3D12Context::CreateSwapChain(HWND hwnd) {
	// SwapChainの設定を行う
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = WinConfig::kClientWidth;
	swapChainDesc.Height = WinConfig::kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = kBackBufferCount;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;

	// CommandQueue、ウィンドウハンドル、設定を渡してSwapChainを作成する
	HRESULT hr = dxgiFactory_->CreateSwapChainForHwnd(
		commandQueue_.Get(),
		hwnd,
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
	resourceDesc.Width = WinConfig::kClientWidth;
	resourceDesc.Height = WinConfig::kClientHeight;
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
) const {
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
) const {
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
	viewport_.Width = static_cast<float>(WinConfig::kClientWidth);
	viewport_.Height = static_cast<float>(WinConfig::kClientHeight);
	viewport_.TopLeftX = 0.0f;
	viewport_.TopLeftY = 0.0f;
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;

	// Scissorを設定する
	scissorRect_.left = 0;
	scissorRect_.right = WinConfig::kClientWidth;
	scissorRect_.top = 0;
	scissorRect_.bottom = WinConfig::kClientHeight;

	Log("Complete create Viewport and Scissor!!!\n");
}

void D3D12Context::WaitForGpu() {
	// Fence値を1つ進める
	++fenceValue_;

	const UINT64 waitFenceValue = fenceValue_;

	// CommandQueueにSignalを送る
	// GPUがここまで実行し終わったら、FenceにwaitFenceValueを書き込む
	HRESULT hr = commandQueue_->Signal(fence_.Get(), waitFenceValue);
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

} // namespace Homura
