#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>

#include <cstddef>
#include <string>

#include "DebugGui.h"
#include "MathUtil.h"
#include "TextureManager.h"

class DXCommon {
public:
	bool Initialize(HWND hwnd);
	void Finalize();

	void Draw();

private:
	struct Material {
		Vector4 color;
	};

	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
	};

	struct TransformationMatrix {
		Matrix4x4 WVP;
	};

private:
	static constexpr UINT kBackBufferCount = 2;
	static constexpr UINT kSRVDescriptorCount = 128;
	static constexpr UINT kDSVDescriptorCount = 1;
	static constexpr UINT kImGuiSRVIndex = 0;
	static constexpr UINT kTextureSRVIndex = 1;

private:
	void EnableDebugLayer();
	void SetupDebugInfoQueue();
	void ReportLiveObjects();

	bool CreateFactory();
	bool SelectAdapter();
	bool CreateDevice();
	bool CreateCommandQueue();
	bool CreateCommandList();
	bool CreateSwapChain(HWND hwnd);
	bool CreateRTVDescriptorHeap();
	bool CreateSRVDescriptorHeap();
	bool CreateDSVDescriptorHeap();
	bool GetSwapChainResources();
	bool CreateRTV();
	bool CreateDepthStencilResource();
	bool CreateDSV();
	bool CreateFence();
	bool CreateTexture();
	bool CreateDebugGui(HWND hwnd);

	bool CreateGraphicsPipelineState();
	ID3D12DescriptorHeap* CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE heapType,
		UINT numDescriptors,
		bool shaderVisible
	);
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUDescriptorHandle(UINT index) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(UINT index) const;
	ID3D12Resource* CreateBufferResource(size_t sizeInBytes);
	bool CreateVertexResource();
	bool CreateSpriteResource();
	bool CreateMaterialResource();
	bool CreateMaterialResourceSprite();
	bool CreateTransformationMatrixResource();
	bool CreateSpriteTransformationMatrixResource();
	void CreateViewportAndScissor();

	IDxcBlob* CompileShader(
		const std::wstring& filePath,
		const wchar_t* profile,
		IDxcUtils* dxcUtils,
		IDxcCompiler3* dxcCompiler,
		IDxcIncludeHandler* includeHandler
	);

	void UpdateTransformationMatrix();
	void UpdateSpriteTransformationMatrix();

	void WaitForGpu();

private:
	IDXGIFactory7* dxgiFactory_ = nullptr;
	IDXGIAdapter4* useAdapter_ = nullptr;
	ID3D12Device* device_ = nullptr;

	ID3D12CommandQueue* commandQueue_ = nullptr;
	ID3D12CommandAllocator* commandAllocator_ = nullptr;
	ID3D12GraphicsCommandList* commandList_ = nullptr;

	IDXGISwapChain4* swapChain_ = nullptr;
	ID3D12Resource* swapChainResources_[kBackBufferCount] = {};

	ID3D12DescriptorHeap* rtvDescriptorHeap_ = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[kBackBufferCount] = {};
	UINT rtvDescriptorSize_ = 0;

	ID3D12DescriptorHeap* srvDescriptorHeap_ = nullptr;
	UINT srvDescriptorSize_ = 0;

	ID3D12DescriptorHeap* dsvDescriptorHeap_ = nullptr;
	ID3D12Resource* depthStencilResource_ = nullptr;

	ID3D12Fence* fence_ = nullptr;
	UINT64 fenceValue_ = 0;
	HANDLE fenceEvent_ = nullptr;

	ID3D12RootSignature* rootSignature_ = nullptr;
	ID3D12PipelineState* graphicsPipelineState_ = nullptr;

	ID3D12Resource* vertexResource_ = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_ = {};

	ID3D12Resource* vertexResourceSprite_ = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite_ = {};

	ID3D12Resource* materialResource_ = nullptr;
	Material* materialData_ = nullptr;

	ID3D12Resource* materialResourceSprite_ = nullptr;
	Material* materialDataSprite_ = nullptr;

	ID3D12Resource* transformationMatrixResource_ = nullptr;
	TransformationMatrix* transformationMatrixData_ = nullptr;

	ID3D12Resource* transformationMatrixResourceSprite_ = nullptr;
	TransformationMatrix* transformationMatrixDataSprite_ = nullptr;

	TextureManager textureManager_;
	DebugGui debugGui_;

	// 3Dモデル用の座標変換行列の初期値
	Transform transform_ = {
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f },
	};

	// スプライト用の座標変換行列の初期値
	Transform transformSprite_ = {
	{ 1.0f, 1.0f, 1.0f },
	{ 0.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f },
	};

	// カメラの座標変換行列の初期値
	Transform cameraTransform_ = {
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, -5.0f },
	};

	D3D12_VIEWPORT viewport_ = {};
	D3D12_RECT scissorRect_ = {};
};