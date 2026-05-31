#include "DxCommon.h"

#include <cassert>
#include <cmath>
#include <format>
#include <d3d12sdklayers.h>
#include <dxgidebug.h>
#include <dxcapi.h>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

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

	// 球の頂点Resourceを作成する
	if (!CreateVertexResource()) {
		return false;
	}

	// Sprite用の頂点Resourceを作成する
	if (!CreateSpriteResource()) {
		return false;
	}

	// マテリアル用のResourceを作成する
	if (!CreateMaterialResource()) {
		return false;
	}

	// Sprite用のマテリアルResourceを作成する
	if (!CreateMaterialResourceSprite()) {
		return false;
	}

	// 座標変換用のResourceを作成する
	if (!CreateTransformationMatrixResource()) {
		return false;
	}

	// Sprite用の座標変換Resourceを作成する
	if (!CreateSpriteTransformationMatrixResource()) {
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
	textureManagerMonsterBall_.Finalize();
	textureManager_.Finalize();

	if (transformationMatrixResource_ != nullptr) {
		transformationMatrixResource_->Release();
		transformationMatrixResource_ = nullptr;
		transformationMatrixData_ = nullptr;
	}

	if (transformationMatrixResourceSprite_ != nullptr) {
		transformationMatrixResourceSprite_->Release();
		transformationMatrixResourceSprite_ = nullptr;
		transformationMatrixDataSprite_ = nullptr;
	}

	if (materialResourceSprite_ != nullptr) {
		materialResourceSprite_->Release();
		materialResourceSprite_ = nullptr;
		materialDataSprite_ = nullptr;
	}

	if (materialResource_ != nullptr) {
		materialResource_->Release();
		materialResource_ = nullptr;
		materialData_ = nullptr;
	}

	if (vertexResourceSprite_ != nullptr) {
		vertexResourceSprite_->Release();
		vertexResourceSprite_ = nullptr;
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

	if (depthStencilResource_ != nullptr) {
		depthStencilResource_->Release();
		depthStencilResource_ = nullptr;
	}

	if (dsvDescriptorHeap_ != nullptr) {
		dsvDescriptorHeap_->Release();
		dsvDescriptorHeap_ = nullptr;
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

	// DSV用DescriptorHeapの先頭ハンドルを取得する
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
		dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

	// これから描画するRTVとDSVを設定する
	commandList_->OMSetRenderTargets(1, &rtvHandles_[backBufferIndex], false, &dsvHandle);

	// 指定した色で画面全体をクリアする
	const float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
	commandList_->ClearRenderTargetView(rtvHandles_[backBufferIndex], clearColor, 0, nullptr);

	// 深度値を一番奥の1.0fでクリアする
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

	// 座標変換行列を更新する
	UpdateTransformationMatrix();

	// Sprite用の座標変換行列を更新する
	UpdateSpriteTransformationMatrix();

	// ImGuiのフレームを開始して開発用UIを作る
	debugGui_.BeginFrame();

#ifdef USE_IMGUI
	ImGui::Begin("Settings");

	if (materialData_ != nullptr) {
		ImGui::ColorEdit4("sphere material", &materialData_->color.x);
	}

	if (materialDataSprite_ != nullptr) {
		ImGui::ColorEdit4("sprite material", &materialDataSprite_->color.x);
	}

	ImGui::DragFloat3("sphere translate", &transform_.translate.x, 0.01f);
	ImGui::DragFloat3("sphere rotate", &transform_.rotate.x, 0.01f);
	ImGui::DragFloat3("sphere scale", &transform_.scale.x, 0.01f);

	ImGui::DragFloat3("camera translate", &cameraTransform_.translate.x, 0.01f);
	ImGui::DragFloat3("camera rotate", &cameraTransform_.rotate.x, 0.01f);

	ImGui::DragFloat3("translateSprite", &transformSprite_.translate.x, 1.0f);
	ImGui::DragFloat3("scaleSprite", &transformSprite_.scale.x, 0.01f);
	ImGui::Checkbox("useMonsterBall", &useMonsterBall_);

	ImGui::End();
#else
	debugGui_.ShowDemoWindow();
#endif
	debugGui_.EndFrame();

	// Shaderと描画設定をまとめたPipelineStateを設定する
	commandList_->SetGraphicsRootSignature(rootSignature_);
	commandList_->SetPipelineState(graphicsPipelineState_);

	// Shaderから参照するDescriptorHeapを設定する
	ID3D12DescriptorHeap* descriptorHeaps[] = {
		srvDescriptorHeap_
	};
	commandList_->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// 球の頂点データをInputAssemblerへ渡す
	commandList_->IASetVertexBuffers(0, 1, &vertexBufferView_);

	// 三角形リストとして球を描画する
	commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// PixelShaderで使うマテリアルCBufferの場所を設定する
	commandList_->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

	// VertexShaderで使う座標変換CBufferの場所を設定する
	commandList_->SetGraphicsRootConstantBufferView(1, transformationMatrixResource_->GetGPUVirtualAddress());

	// PixelShaderで使うTexture用SRVのDescriptorTableを設定する
	// useMonsterBall_がtrueならMonsterBall、falseならuvCheckerを使う
	commandList_->SetGraphicsRootDescriptorTable(
		2,
		useMonsterBall_
		? textureManagerMonsterBall_.GetTextureSrvHandleGPU()
		: textureManager_.GetTextureSrvHandleGPU()
	);

	// 実際に球の描画命令を積む
	commandList_->DrawInstanced(kSphereVertexCount, 1, 0, 0);

	// Spriteの描画
	commandList_->IASetVertexBuffers(0, 1, &vertexBufferViewSprite_);

	// Sprite用のTransformationMatrixCBufferの場所を設定する
	commandList_->SetGraphicsRootConstantBufferView(
		1,
		transformationMatrixResourceSprite_->GetGPUVirtualAddress()
	);
	// Sprite用のマテリアルCBufferの場所を設定する
	commandList_->SetGraphicsRootConstantBufferView(0, materialResourceSprite_->GetGPUVirtualAddress());

	// Spriteは常にuvCheckerを使うようにSRVを設定し直す
	commandList_->SetGraphicsRootDescriptorTable(2, textureManager_.GetTextureSrvHandleGPU());

	// 実際にSpriteの描画命令を積む
	commandList_->DrawInstanced(6, 1, 0, 0);

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

bool DXCommon::CreateDSVDescriptorHeap() {
	// DSV用のDescriptorHeapを作成する
	dsvDescriptorHeap_ = CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
		kDSVDescriptorCount,
		false
	);

	if (dsvDescriptorHeap_ == nullptr) {
		return false;
	}

	Log("Complete create DSV DescriptorHeap!!!\n");

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

bool DXCommon::CreateDSV() {
	if (depthStencilResource_ == nullptr || dsvDescriptorHeap_ == nullptr) {
		return false;
	}

	// DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	// DSVHeapの先頭にDSVを作る
	device_->CreateDepthStencilView(
		depthStencilResource_,
		&dsvDesc,
		dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart()
	);

	Log("Complete create DSV!!!\n");

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

bool DXCommon::CreateDepthStencilResource() {
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
	fenceEvent_ = CreateEvent(nullptr, false, false, nullptr);
	assert(fenceEvent_ != nullptr);

	if (fenceEvent_ == nullptr) {
		return false;
	}

	Log("Complete create Fence!!!\n");
	return true;
}

bool DXCommon::CreateTexture() {
	// resources内のTextureを読み込んでShaderから使えるようにする
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

	ID3D12Resource* uvCheckerIntermediateResource = nullptr;
	ID3D12Resource* monsterBallIntermediateResource = nullptr;

	// 1枚目のTextureとしてuvCheckerを読み込む
	if (!textureManager_.Initialize(
		device_,
		commandList_,
		"Resources/uvChecker.png",
		GetSRVCPUDescriptorHandle(kTextureSRVIndex),
		GetSRVGPUDescriptorHandle(kTextureSRVIndex),
		&uvCheckerIntermediateResource
	)) {
		if (uvCheckerIntermediateResource != nullptr) {
			uvCheckerIntermediateResource->Release();
			uvCheckerIntermediateResource = nullptr;
		}

		if (monsterBallIntermediateResource != nullptr) {
			monsterBallIntermediateResource->Release();
			monsterBallIntermediateResource = nullptr;
		}

		return false;
	}

	// 2枚目のTextureとしてmonsterBallを読み込む
	if (!textureManagerMonsterBall_.Initialize(
		device_,
		commandList_,
		"Resources/monsterBall.png",
		GetSRVCPUDescriptorHandle(kMonsterBallTextureSRVIndex),
		GetSRVGPUDescriptorHandle(kMonsterBallTextureSRVIndex),
		&monsterBallIntermediateResource
	)) {
		if (uvCheckerIntermediateResource != nullptr) {
			uvCheckerIntermediateResource->Release();
			uvCheckerIntermediateResource = nullptr;
		}

		if (monsterBallIntermediateResource != nullptr) {
			monsterBallIntermediateResource->Release();
			monsterBallIntermediateResource = nullptr;
		}

		return false;
	}

	// Texture転送のCommandListを確定してGPUに実行してもらう
	hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		if (uvCheckerIntermediateResource != nullptr) {
			uvCheckerIntermediateResource->Release();
			uvCheckerIntermediateResource = nullptr;
		}

		if (monsterBallIntermediateResource != nullptr) {
			monsterBallIntermediateResource->Release();
			monsterBallIntermediateResource = nullptr;
		}

		return false;
	}

	ID3D12CommandList* commandLists[] = { commandList_ };
	commandQueue_->ExecuteCommandLists(1, commandLists);

	// 転送が完了するまで中間Resourceは保持しておく
	WaitForGpu();

	if (uvCheckerIntermediateResource != nullptr) {
		uvCheckerIntermediateResource->Release();
		uvCheckerIntermediateResource = nullptr;
	}

	if (monsterBallIntermediateResource != nullptr) {
		monsterBallIntermediateResource->Release();
		monsterBallIntermediateResource = nullptr;
	}

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
	IDxcIncludeHandler* includeHandler = nullptr;

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

	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		dxcCompiler->Release();
		dxcCompiler = nullptr;

		dxcUtils->Release();
		dxcUtils = nullptr;

		return false;
	}

	// Shaderをコンパイルする
	IDxcBlob* vertexShaderBlob = CompileShader(
		L"Object3d.VS.hlsl",
		L"vs_6_0",
		dxcUtils,
		dxcCompiler,
		includeHandler
	);
	assert(vertexShaderBlob != nullptr);

	if (vertexShaderBlob == nullptr) {
		includeHandler->Release();
		includeHandler = nullptr;

		dxcCompiler->Release();
		dxcCompiler = nullptr;

		dxcUtils->Release();
		dxcUtils = nullptr;

		return false;
	}

	IDxcBlob* pixelShaderBlob = CompileShader(
		L"Object3d.PS.hlsl",
		L"ps_6_0",
		dxcUtils,
		dxcCompiler,
		includeHandler
	);
	assert(pixelShaderBlob != nullptr);

	if (pixelShaderBlob == nullptr) {
		vertexShaderBlob->Release();
		vertexShaderBlob = nullptr;

		includeHandler->Release();
		includeHandler = nullptr;

		dxcCompiler->Release();
		dxcCompiler = nullptr;

		dxcUtils->Release();
		dxcUtils = nullptr;

		return false;
	}

	// RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// Texture用のDescriptorRangeを作成する
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart =
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// RootParameter作成。PixelShaderのMaterial、VertexShaderのTransform、PixelShaderのTexture
	D3D12_ROOT_PARAMETER rootParameters[3] = {};

	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 0;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	// Samplerの設定
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

	// シリアライズしてバイナリにする
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

		assert(false);
		return false;
	}

	// バイナリを元に生成
	hr = device_->CreateRootSignature(
		0,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		signatureBlob->Release();
		signatureBlob = nullptr;

		if (errorBlob != nullptr) {
			errorBlob->Release();
			errorBlob = nullptr;
		}

		return false;
	}

	// InputLayoutの設定
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[2] = {};

	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// BlendStateの設定
	D3D12_BLEND_DESC blendDesc{};

	// すべての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;

	// RasterizerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};

	// 裏面を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;

	// 三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};

	// Depthの機能を有効にする
	depthStencilDesc.DepthEnable = true;

	// 書き込みします
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

	// 比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// PSOを生成する
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

	// DepthStencilをPSOへ設定する
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// 利用するトポロジのタイプ。今回は三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// どのように画面に色を打ち込むかの設定
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// 実際に生成
	hr = device_->CreateGraphicsPipelineState(
		&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		signatureBlob->Release();
		signatureBlob = nullptr;

		if (errorBlob != nullptr) {
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

		return false;
	}

	// もう使わないResourceを解放
	signatureBlob->Release();
	signatureBlob = nullptr;

	if (errorBlob != nullptr) {
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

D3D12_CPU_DESCRIPTOR_HANDLE DXCommon::GetCPUDescriptorHandle(
	ID3D12DescriptorHeap* descriptorHeap,
	UINT descriptorSize,
	UINT index
) const {
	// 指定したDescriptorHeapの指定した位置のCPUHandleを取得する
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU =
		descriptorHeap->GetCPUDescriptorHandleForHeapStart();

	handleCPU.ptr += static_cast<SIZE_T>(descriptorSize) * index;

	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE DXCommon::GetGPUDescriptorHandle(
	ID3D12DescriptorHeap* descriptorHeap,
	UINT descriptorSize,
	UINT index
) const {
	// 指定したDescriptorHeapの指定した位置のGPUHandleを取得する
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU =
		descriptorHeap->GetGPUDescriptorHandleForHeapStart();

	handleGPU.ptr += static_cast<SIZE_T>(descriptorSize) * index;

	return handleGPU;
}

D3D12_CPU_DESCRIPTOR_HANDLE DXCommon::GetSRVCPUDescriptorHandle(UINT index) const {
	// SRV用DescriptorHeapの指定した位置のCPUHandleを取得する
	return GetCPUDescriptorHandle(
		srvDescriptorHeap_,
		srvDescriptorSize_,
		index
	);
}

D3D12_GPU_DESCRIPTOR_HANDLE DXCommon::GetSRVGPUDescriptorHandle(UINT index) const {
	// SRV用DescriptorHeapの指定した位置のGPUHandleを取得する
	return GetGPUDescriptorHandle(
		srvDescriptorHeap_,
		srvDescriptorSize_,
		index
	);
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
	// 球用の頂点Resourceを作成する
	vertexResource_ = CreateBufferResource(sizeof(VertexData) * kSphereVertexCount);
	assert(vertexResource_ != nullptr);

	if (vertexResource_ == nullptr) {
		return false;
	}

	// 頂点Resourceにデータを書き込む
	VertexData* vertexData = nullptr;

	// 書き込み用のアドレスを取得する
	HRESULT hr = vertexResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&vertexData)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// 円周率
	const float kPi = 3.14159265358979323846f;

	// 経度方向の1分割分の角度
	const float kLonEvery = kPi * 2.0f / float(kSphereSubdivision);

	// 緯度方向の1分割分の角度
	const float kLatEvery = kPi / float(kSphereSubdivision);

	// 緯度方向に分割する
	for (uint32_t latIndex = 0; latIndex < kSphereSubdivision; ++latIndex) {
		// 緯度。下から上へ作っていくため、最初は -π/2
		float lat = -kPi / 2.0f + kLatEvery * float(latIndex);

		// 経度方向に分割する
		for (uint32_t lonIndex = 0; lonIndex < kSphereSubdivision; ++lonIndex) {
			// 今から書き込む頂点の先頭番号
			uint32_t start = (latIndex * kSphereSubdivision + lonIndex) * 6;

			// 経度
			float lon = kLonEvery * float(lonIndex);

			// 基準点a
			Vector4 a = {
				std::cos(lat) * std::cos(lon),
				std::sin(lat),
				std::cos(lat) * std::sin(lon),
				1.0f
			};

			// 基準点b
			Vector4 b = {
				std::cos(lat + kLatEvery) * std::cos(lon),
				std::sin(lat + kLatEvery),
				std::cos(lat + kLatEvery) * std::sin(lon),
				1.0f
			};

			// 基準点c
			Vector4 c = {
				std::cos(lat) * std::cos(lon + kLonEvery),
				std::sin(lat),
				std::cos(lat) * std::sin(lon + kLonEvery),
				1.0f
			};

			// 基準点d
			Vector4 d = {
				std::cos(lat + kLatEvery) * std::cos(lon + kLonEvery),
				std::sin(lat + kLatEvery),
				std::cos(lat + kLatEvery) * std::sin(lon + kLonEvery),
				1.0f
			};

			// Texture用のUV座標を計算する
			float u = float(lonIndex) / float(kSphereSubdivision);
			float v = 1.0f - float(latIndex) / float(kSphereSubdivision);
			float nextU = float(lonIndex + 1) / float(kSphereSubdivision);
			float nextV = 1.0f - float(latIndex + 1) / float(kSphereSubdivision);

			// 1枚目の三角形 a, b, c
			vertexData[start + 0].position = a;
			vertexData[start + 0].texcoord = { u, v };

			vertexData[start + 1].position = b;
			vertexData[start + 1].texcoord = { u, nextV };

			vertexData[start + 2].position = c;
			vertexData[start + 2].texcoord = { nextU, v };

			// 2枚目の三角形 c, b, d
			vertexData[start + 3].position = c;
			vertexData[start + 3].texcoord = { nextU, v };

			vertexData[start + 4].position = b;
			vertexData[start + 4].texcoord = { u, nextV };

			vertexData[start + 5].position = d;
			vertexData[start + 5].texcoord = { nextU, nextV };
		}
	}

	// 頂点バッファビューを作成する
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * kSphereVertexCount);
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	Log("Complete create Sphere VertexResource!!!\n");

	return true;
}

bool DXCommon::CreateSpriteResource() {
	// Sprite用の頂点Resourceを作成する。矩形なので三角形2枚分の6頂点を用意する
	vertexResourceSprite_ = CreateBufferResource(sizeof(VertexData) * 6);
	assert(vertexResourceSprite_ != nullptr);

	if (vertexResourceSprite_ == nullptr) {
		return false;
	}

	// Sprite用の頂点Resourceにデータを書き込む
	VertexData* vertexDataSprite = nullptr;

	// 書き込み用のアドレスを取得する
	HRESULT hr = vertexResourceSprite_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&vertexDataSprite)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// 1枚目の三角形
	vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f, 1.0f };		// 左下
	vertexDataSprite[0].texcoord = { 0.0f, 1.0f };

	vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };		// 左上
	vertexDataSprite[1].texcoord = { 0.0f, 0.0f };

	vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f, 1.0f };	// 右下
	vertexDataSprite[2].texcoord = { 1.0f, 1.0f };

	// 2枚目の三角形
	vertexDataSprite[3].position = { 0.0f, 0.0f, 0.0f, 1.0f };		// 左上
	vertexDataSprite[3].texcoord = { 0.0f, 0.0f };

	vertexDataSprite[4].position = { 640.0f, 0.0f, 0.0f, 1.0f };		// 右上
	vertexDataSprite[4].texcoord = { 1.0f, 0.0f };

	vertexDataSprite[5].position = { 640.0f, 360.0f, 0.0f, 1.0f };	// 右下
	vertexDataSprite[5].texcoord = { 1.0f, 1.0f };

	// Sprite用の頂点バッファビューを作成する
	vertexBufferViewSprite_.BufferLocation = vertexResourceSprite_->GetGPUVirtualAddress();
	vertexBufferViewSprite_.SizeInBytes = sizeof(VertexData) * 6;
	vertexBufferViewSprite_.StrideInBytes = sizeof(VertexData);

	Log("Complete create Sprite VertexResource!!!\n");

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
	materialData_ = nullptr;

	// 書き込み用のアドレスを取得する
	HRESULT hr = materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// Textureの色をそのまま見やすくするため白色を設定する
	materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };

	Log("Complete create MaterialResource!!!\n");

	return true;
}

bool DXCommon::CreateMaterialResourceSprite() {
	// Sprite用のマテリアルResourceを作成する
	materialResourceSprite_ = CreateBufferResource(sizeof(Material));
	assert(materialResourceSprite_ != nullptr);

	if (materialResourceSprite_ == nullptr) {
		return false;
	}

	// Sprite用のマテリアルResourceにデータを書き込む
	materialDataSprite_ = nullptr;

	// 書き込み用のアドレスを取得する
	HRESULT hr = materialResourceSprite_->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite_));
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// SpriteもTextureの色をそのまま見やすくするため白色を設定する
	materialDataSprite_->color = { 1.0f, 1.0f, 1.0f, 1.0f };

	Log("Complete create Sprite MaterialResource!!!\n");

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

bool DXCommon::CreateSpriteTransformationMatrixResource() {
	// Sprite用の座標変換Resourceを作成する
	transformationMatrixResourceSprite_ = CreateBufferResource(sizeof(TransformationMatrix));
	assert(transformationMatrixResourceSprite_ != nullptr);

	if (transformationMatrixResourceSprite_ == nullptr) {
		return false;
	}

	// Sprite用の座標変換Resourceにデータを書き込む
	transformationMatrixDataSprite_ = nullptr;

	// 書き込み用のアドレスを取得する
	HRESULT hr = transformationMatrixResourceSprite_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&transformationMatrixDataSprite_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// 最初は単位行列を設定する
	transformationMatrixDataSprite_->WVP = MakeIdentity4x4();

	Log("Complete create Sprite TransformationMatrixResource!!!\n");

	return true;
}

void DXCommon::UpdateTransformationMatrix() {
	if (transformationMatrixData_ == nullptr) {
		return;
	}

	// Y軸回転で球を動かす
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

void DXCommon::UpdateSpriteTransformationMatrix() {
	if (transformationMatrixDataSprite_ == nullptr) {
		return;
	}

	// Sprite用のWorldMatrixを作成する
	Matrix4x4 worldMatrixSprite = MakeAffineMatrix(
		transformSprite_.scale,
		transformSprite_.rotate,
		transformSprite_.translate
	);

	// Spriteは2D表示なので、今回はViewMatrixは単位行列にする
	Matrix4x4 viewMatrixSprite = MakeIdentity4x4();

	// 画面座標をそのまま使えるように平行投影行列を作成する
	Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(
		0.0f,
		0.0f,
		float(WinConfig::kClientWidth),
		float(WinConfig::kClientHeight),
		0.0f,
		100.0f
	);

	// World、View、Projectionをまとめる
	Matrix4x4 worldViewProjectionMatrixSprite = Multiply(
		worldMatrixSprite,
		Multiply(viewMatrixSprite, projectionMatrixSprite)
	);

	// VertexShaderへ渡すSprite用の行列を更新する
	transformationMatrixDataSprite_->WVP = worldViewProjectionMatrixSprite;
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
