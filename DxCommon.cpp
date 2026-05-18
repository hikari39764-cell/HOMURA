#include "DxCommon.h"

#include <cassert>
#include <cmath>
#include <format>
#include <d3d12sdklayers.h>
#include <dxgidebug.h>
#include <dxcapi.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

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

	// TextureやImGuiで使うSRV用DescriptorHeapを作成する
	if (!CreateSRVDescriptorHeap()) {
		return false;
	}

	// Textureを読み込んでShaderから使えるようにする
	if (!CreateTexture()) {
		return false;
	}

	// 開発用UIを初期化する
	if (!CreateDebugGui(hwnd)) {
		return false;
	}

	// 三角形を描画するためのPipelineStateを作成する
	if (!CreateGraphicsPipelineState()) {
		return false;
	}

	// 三角形の頂点Resourceを作成する
	if (!CreateVertexResource()) {
		return false;
	}

	// マテリアル用のResourceを作成する
	if (!CreateMaterialResource()) {
		return false;
	}

	// 座標変換用のResourceを作成する
	if (!CreateTransformationMatrixResource()) {
		return false;
	}

	// 画面全体に描画するためのViewportとScissorを設定する
	CreateViewportAndScissor();

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

	debugGui_.Finalize();
	textureManager_.Finalize();

	if (transformationMatrixResource_ != nullptr) {
		transformationMatrixResource_->Release();
		transformationMatrixResource_ = nullptr;
		transformationMatrixData_ = nullptr;
	}

	if (materialResource_ != nullptr) {
		materialResource_->Release();
		materialResource_ = nullptr;
	}

	if (vertexResource_ != nullptr) {
		vertexResource_->Release();
		vertexResource_ = nullptr;
	}

	if (graphicsPipelineState_ != nullptr) {
		graphicsPipelineState_->Release();
		graphicsPipelineState_ = nullptr;
	}

	if (rootSignature_ != nullptr) {
		rootSignature_->Release();
		rootSignature_ = nullptr;
	}

	if (fenceEvent_ != nullptr) {
		CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
	}

	if (fence_ != nullptr) {
		fence_->Release();
		fence_ = nullptr;
	}

	if (srvDescriptorHeap_ != nullptr) {
		srvDescriptorHeap_->Release();
		srvDescriptorHeap_ = nullptr;
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

	// 描画範囲を設定する
	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissorRect_);

	// 座標変換行列を更新する
	UpdateTransformationMatrix();

	// ImGuiのフレームを開始して開発用UIを作る
	debugGui_.BeginFrame();
	debugGui_.ShowDemoWindow();
	debugGui_.EndFrame();

	// Shaderと描画設定をまとめたPipelineStateを設定する
	commandList_->SetGraphicsRootSignature(rootSignature_);
	commandList_->SetPipelineState(graphicsPipelineState_);

	// Shaderから参照するDescriptorHeapを設定する
	ID3D12DescriptorHeap* descriptorHeaps[] = {
		srvDescriptorHeap_
	};
	commandList_->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// 三角形の頂点データをInputAssemblerへ渡す
	commandList_->IASetVertexBuffers(0, 1, &vertexBufferView_);

	// 3頂点で1つの三角形を描画する
	commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// PixelShaderで使うマテリアルCBufferの場所を設定する
	commandList_->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

	// VertexShaderで使う座標変換CBufferの場所を設定する
	commandList_->SetGraphicsRootConstantBufferView(1, transformationMatrixResource_->GetGPUVirtualAddress());

	// PixelShaderで使うTexture用SRVのDescriptorTableを設定する
	commandList_->SetGraphicsRootDescriptorTable(2, textureManager_.GetTextureSrvHandleGPU());

	// 実際に描画命令を積む
	commandList_->DrawInstanced(3, 1, 0, 0);

	// 最後にImGuiを描画して画面の前面に表示する
	debugGui_.Render(commandList_);

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
	rtvDescriptorHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kBackBufferCount, false);

	if (rtvDescriptorHeap_ == nullptr) {
		return false;
	}

	// Descriptorの1個分のサイズを取得する
	rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	Log("Complete create RTV DescriptorHeap!!!\n");
	return true;
}

bool DXCommon::CreateSRVDescriptorHeap() {
	// SRV用のHeapでDescriptorの数は128。Shader内で触るのでShaderVisibleはtrue
	srvDescriptorHeap_ = CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		kSRVDescriptorCount,
		true
	);

	if (srvDescriptorHeap_ == nullptr) {
		return false;
	}

	// Descriptorの1個分のサイズを取得する
	srvDescriptorSize_ =
		device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	Log("Complete create SRV DescriptorHeap!!!\n");
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

bool DXCommon::CreateTexture() {
	// resources内のuvCheckerを読み込んでShaderから使えるようにする
	// Texture転送用のCommandListを記録できる状態にする
	HRESULT hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	hr = commandList_->Reset(commandAllocator_, nullptr);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	ID3D12Resource* intermediateResource = nullptr;

	if (!textureManager_.Initialize(
		device_,
		commandList_,
		"Resources/uvChecker.png",
		GetSRVCPUDescriptorHandle(kTextureSRVIndex),
		GetSRVGPUDescriptorHandle(kTextureSRVIndex),
		&intermediateResource
	)) {
		if (intermediateResource != nullptr) {
			intermediateResource->Release();
			intermediateResource = nullptr;
		}

		return false;
	}

	// Texture転送のCommandListを確定してGPUに実行してもらう
	hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		intermediateResource->Release();
		intermediateResource = nullptr;
		return false;
	}

	ID3D12CommandList* commandLists[] = { commandList_ };
	commandQueue_->ExecuteCommandLists(1, commandLists);

	// 転送が完了するまで中間Resourceは保持しておく
	WaitForGpu();

	intermediateResource->Release();
	intermediateResource = nullptr;

	return true;
}

bool DXCommon::CreateDebugGui(HWND hwnd) {
	// ImGuiはSRV用Heapの先頭を使ってフォント用Textureを管理する
	if (!debugGui_.Initialize(
		hwnd,
		device_,
		kBackBufferCount,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		srvDescriptorHeap_,
		GetSRVCPUDescriptorHandle(kImGuiSRVIndex),
		GetSRVGPUDescriptorHandle(kImGuiSRVIndex)
	)) {
		return false;
	}

	return true;
}

IDxcBlob* DXCommon::CompileShader(
	const std::wstring& filePath,
	const wchar_t* profile,
	IDxcUtils* dxcUtils,
	IDxcCompiler3* dxcCompiler,
	IDxcIncludeHandler* includeHandler
) {
	// ログ
	Log(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile));

	// hlslファイルを読み込む
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return nullptr;
	}

	// 読み込んだファイルの内容をDXCへ渡す
	DxcBuffer shaderSourceBuffer{};
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;

	// Compileに使う設定
	LPCWSTR arguments[] = {
		filePath.c_str(),	// Compile対象のhlslファイル名
		L"-E", L"main",		// EntryPoint。基本的にmain
		L"-T", profile,		// ShaderProfile
		L"-Zi", L"-Qembed_debug",	// Debug情報を埋め込む
		L"-Od",				// 最適化を外しておく
		L"-Zpr",			// メモリレイアウトは行優先
	};

	// 実際にShaderをCompileする
	IDxcResult* shaderResult = nullptr;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer,
		arguments,
		_countof(arguments),
		includeHandler,
		IID_PPV_ARGS(&shaderResult)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		shaderSource->Release();
		shaderSource = nullptr;
		return nullptr;
	}

	// 警告又はエラーが出ていないか確認する
	IDxcBlobUtf8* shaderError = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	assert(SUCCEEDED(hr));

	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Log(shaderError->GetStringPointer());

		shaderError->Release();
		shaderError = nullptr;

		shaderSource->Release();
		shaderSource = nullptr;

		shaderResult->Release();
		shaderResult = nullptr;

		// Shaderの警告又はエラーは必ず直す
		assert(false);
		return nullptr;
	}

	if (shaderError != nullptr) {
		shaderError->Release();
		shaderError = nullptr;
	}

	// Compile自体が成功しているか確認する
	HRESULT compileStatus = S_OK;
	hr = shaderResult->GetStatus(&compileStatus);
	assert(SUCCEEDED(hr));
	assert(SUCCEEDED(compileStatus));

	if (FAILED(hr) || FAILED(compileStatus)) {
		shaderSource->Release();
		shaderSource = nullptr;

		shaderResult->Release();
		shaderResult = nullptr;

		return nullptr;
	}

	// Compile結果から実行用のバイナリ部分を取得する
	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	assert(shaderBlob != nullptr);

	if (FAILED(hr) || shaderBlob == nullptr) {
		shaderSource->Release();
		shaderSource = nullptr;

		shaderResult->Release();
		shaderResult = nullptr;

		return nullptr;
	}

	// 成功したらログを出す
	Log(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile));

	// リソースを解放する
	shaderSource->Release();
	shaderSource = nullptr;

	shaderResult->Release();
	shaderResult = nullptr;

	// 実行用のバイナリを返す
	return shaderBlob;
}

bool DXCommon::CreateGraphicsPipelineState() {
	// DXCを初期化する
	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;

	HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		dxcUtils->Release();
		dxcUtils = nullptr;
		return false;
	}

	// includeに対応するための設定を作る
	IDxcIncludeHandler* includeHandler = nullptr;
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		dxcCompiler->Release();
		dxcCompiler = nullptr;

		dxcUtils->Release();
		dxcUtils = nullptr;

		return false;
	}

	// ShaderをCompileする
	IDxcBlob* vertexShaderBlob = CompileShader(
		L"Object3d.VS.hlsl",
		L"vs_6_0",
		dxcUtils,
		dxcCompiler,
		includeHandler
	);
	assert(vertexShaderBlob != nullptr);

	IDxcBlob* pixelShaderBlob = CompileShader(
		L"Object3d.PS.hlsl",
		L"ps_6_0",
		dxcUtils,
		dxcCompiler,
		includeHandler
	);
	assert(pixelShaderBlob != nullptr);

	if (vertexShaderBlob == nullptr || pixelShaderBlob == nullptr) {
		if (pixelShaderBlob != nullptr) {
			pixelShaderBlob->Release();
			pixelShaderBlob = nullptr;
		}

		if (vertexShaderBlob != nullptr) {
			vertexShaderBlob->Release();
			vertexShaderBlob = nullptr;
		}

		includeHandler->Release();
		includeHandler = nullptr;

		dxcCompiler->Release();
		dxcCompiler = nullptr;

		dxcUtils->Release();
		dxcUtils = nullptr;

		return false;
	}

	// RootSignatureを作成する
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// RootParameter作成。PixelShaderのMaterial、VertexShaderのTransform、Texture用SRV
	D3D12_ROOT_PARAMETER rootParameters[3] = {};

	// PixelShaderで使うMaterial用CBuffer
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;

	// VertexShaderで使う座標変換用CBuffer
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 0;

	// PixelShaderでTextureを読むためのDescriptorRange
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// PixelShaderで使うTexture用DescriptorTable
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	// TextureをSamplingするためのSamplerを設定する
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[0].ShaderRegister = 0;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	// RootSignatureをシリアライズしてバイナリにする
	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;

	hr = D3D12SerializeRootSignature(
		&descriptionRootSignature,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&signatureBlob,
		&errorBlob
	);

	if (FAILED(hr)) {
		if (errorBlob != nullptr) {
			Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
			errorBlob->Release();
			errorBlob = nullptr;
		}

		vertexShaderBlob->Release();
		vertexShaderBlob = nullptr;

		pixelShaderBlob->Release();
		pixelShaderBlob = nullptr;

		includeHandler->Release();
		includeHandler = nullptr;

		dxcCompiler->Release();
		dxcCompiler = nullptr;

		dxcUtils->Release();
		dxcUtils = nullptr;

		assert(false);
		return false;
	}

	// バイナリを元にRootSignatureを生成する
	hr = device_->CreateRootSignature(
		0,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature_)
	);
	assert(SUCCEEDED(hr));

	signatureBlob->Release();
	signatureBlob = nullptr;

	if (errorBlob != nullptr) {
		errorBlob->Release();
		errorBlob = nullptr;
	}

	if (FAILED(hr)) {
		vertexShaderBlob->Release();
		vertexShaderBlob = nullptr;

		pixelShaderBlob->Release();
		pixelShaderBlob = nullptr;

		includeHandler->Release();
		includeHandler = nullptr;

		dxcCompiler->Release();
		dxcCompiler = nullptr;

		dxcUtils->Release();
		dxcUtils = nullptr;

		return false;
	}

	// InputLayoutを設定する
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[2] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].InputSlot = 0;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	inputElementDescs[0].InstanceDataStepRate = 0;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].InputSlot = 0;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	inputElementDescs[1].InstanceDataStepRate = 0;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// BlendStateを設定する
	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// RasterizerStateを設定する
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// GraphicsPipelineStateを設定する
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature_;
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
	graphicsPipelineStateDesc.VS = {
		vertexShaderBlob->GetBufferPointer(),
		vertexShaderBlob->GetBufferSize()
	};
	graphicsPipelineStateDesc.PS = {
		pixelShaderBlob->GetBufferPointer(),
		pixelShaderBlob->GetBufferSize()
	};
	graphicsPipelineStateDesc.BlendState = blendDesc;
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;

	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// 利用するトポロジーのタイプ
	graphicsPipelineStateDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// サンプリング設定
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// 実際にPSOを生成する
	hr = device_->CreateGraphicsPipelineState(
		&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState_)
	);
	assert(SUCCEEDED(hr));

	pixelShaderBlob->Release();
	pixelShaderBlob = nullptr;

	vertexShaderBlob->Release();
	vertexShaderBlob = nullptr;

	includeHandler->Release();
	includeHandler = nullptr;

	dxcCompiler->Release();
	dxcCompiler = nullptr;

	dxcUtils->Release();
	dxcUtils = nullptr;

	if (FAILED(hr)) {
		return false;
	}

	Log("Complete create GraphicsPipelineState!!!\n");
	return true;
}

ID3D12DescriptorHeap* DXCommon::CreateDescriptorHeap(
	D3D12_DESCRIPTOR_HEAP_TYPE heapType,
	UINT numDescriptors,
	bool shaderVisible
) {
	// DescriptorHeapの作成処理を共通化する
	ID3D12DescriptorHeap* descriptorHeap = nullptr;

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
		return nullptr;
	}

	return descriptorHeap;
}

D3D12_CPU_DESCRIPTOR_HANDLE DXCommon::GetSRVCPUDescriptorHandle(UINT index) const {
	// SRV用DescriptorHeapの指定した位置のCPUHandleを取得する
	D3D12_CPU_DESCRIPTOR_HANDLE handle = srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += static_cast<SIZE_T>(srvDescriptorSize_) * index;
	return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DXCommon::GetSRVGPUDescriptorHandle(UINT index) const {
	// SRV用DescriptorHeapの指定した位置のGPUHandleを取得する
	D3D12_GPU_DESCRIPTOR_HANDLE handle = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += static_cast<SIZE_T>(srvDescriptorSize_) * index;
	return handle;
}

ID3D12Resource* DXCommon::CreateBufferResource(size_t sizeInBytes) {
	// Buffer用のResourceを作成するためのHeap設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	// Buffer用のResource設定
	D3D12_RESOURCE_DESC bufferResourceDesc{};
	bufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferResourceDesc.Width = sizeInBytes;
	bufferResourceDesc.Height = 1;
	bufferResourceDesc.DepthOrArraySize = 1;
	bufferResourceDesc.MipLevels = 1;
	bufferResourceDesc.SampleDesc.Count = 1;
	bufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	// 実際にBufferResourceを作成する
	ID3D12Resource* bufferResource = nullptr;
	HRESULT hr = device_->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferResourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&bufferResource)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return nullptr;
	}

	return bufferResource;
}

bool DXCommon::CreateVertexResource() {
	// 三角形の頂点Resourceを作成する
	vertexResource_ = CreateBufferResource(sizeof(VertexData) * 3);
	assert(vertexResource_ != nullptr);

	if (vertexResource_ == nullptr) {
		return false;
	}

	// 頂点Resourceにデータを書き込む
	VertexData* vertexData = nullptr;

	// 書き込み用のアドレスを取得する
	HRESULT hr = vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// 左下
	vertexData[0].position = { -0.5f, -0.5f, 0.0f, 1.0f };
	vertexData[0].texcoord = { 0.0f, 1.0f };

	// 上
	vertexData[1].position = { 0.0f, 0.5f, 0.0f, 1.0f };
	vertexData[1].texcoord = { 0.5f, 0.0f };

	// 右下
	vertexData[2].position = { 0.5f, -0.5f, 0.0f, 1.0f };
	vertexData[2].texcoord = { 1.0f, 1.0f };

	// 頂点バッファビューを作成する
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = sizeof(VertexData) * 3;
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	Log("Complete create VertexResource!!!\n");

	return true;
}

bool DXCommon::CreateMaterialResource() {
	// マテリアル用のResourceを作成する
	materialResource_ = CreateBufferResource(sizeof(Material));
	assert(materialResource_ != nullptr);

	if (materialResource_ == nullptr) {
		return false;
	}

	// マテリアルResourceにデータを書き込む
	Material* materialData = nullptr;

	// 書き込み用のアドレスを取得する
	HRESULT hr = materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// Textureの色をそのまま見やすくするため白色を設定する
	materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };

	Log("Complete create MaterialResource!!!\n");

	return true;
}

bool DXCommon::CreateTransformationMatrixResource() {
	// 座標変換用のResourceを作成する
	transformationMatrixResource_ = CreateBufferResource(sizeof(TransformationMatrix));
	assert(transformationMatrixResource_ != nullptr);

	if (transformationMatrixResource_ == nullptr) {
		return false;
	}

	// 座標変換Resourceにデータを書き込む
	transformationMatrixData_ = nullptr;

	// 書き込み用のアドレスを取得する
	HRESULT hr = transformationMatrixResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&transformationMatrixData_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// 最初は単位行列を設定する
	transformationMatrixData_->WVP = MakeIdentity4x4();

	Log("Complete create TransformationMatrixResource!!!\n");

	return true;
}

void DXCommon::UpdateTransformationMatrix() {
	if (transformationMatrixData_ == nullptr) {
		return;
	}

	// 今回はY軸回転で三角形を動かす
	transform_.rotate.y += 0.03f;

	// オブジェクトのWorldMatrixを作成する
	Matrix4x4 worldMatrix = MakeAffineMatrix(
		transform_.scale,
		transform_.rotate,
		transform_.translate
	);

	// カメラのViewMatrixを作成する
	Matrix4x4 cameraMatrix = MakeAffineMatrix(
		cameraTransform_.scale,
		cameraTransform_.rotate,
		cameraTransform_.translate
	);

	Matrix4x4 viewMatrix = Inverse(cameraMatrix);

	// 透視投影行列を作成する
	Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(
		0.45f,
		float(WinConfig::kClientWidth) / float(WinConfig::kClientHeight),
		0.1f,
		100.0f
	);

	// World、View、Projectionをまとめる
	Matrix4x4 worldViewProjectionMatrix = Multiply(
		worldMatrix,
		Multiply(viewMatrix, projectionMatrix)
	);

	// VertexShaderへ渡す行列を更新する
	transformationMatrixData_->WVP = worldViewProjectionMatrix;
}

void DXCommon::CreateViewportAndScissor() {
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
