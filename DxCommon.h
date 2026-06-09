#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>

#include <cstdint>
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
		int32_t enableLighting;
		float padding[3];
		Matrix4x4 uvTransform;
	};

	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};

	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
	};

	struct DirectionalLight {
		Vector4 color;
		Vector3 direction;
		float intensity;
	};

private:

	static constexpr UINT kBackBufferCount = 2;
	static constexpr UINT kSRVDescriptorCount = 128;
	static constexpr UINT kDSVDescriptorCount = 1;
	static constexpr UINT kImGuiSRVIndex = 0;
	static constexpr UINT kTextureSRVIndex = 1;
	static constexpr UINT kMonsterBallTextureSRVIndex = 2;

	// 球の分割数。数を増やすほどなめらかになる
	static constexpr uint32_t kSphereSubdivision = 16;

	// Indexを使うので、経度と緯度の交点だけ頂点を用意する
	static constexpr uint32_t kSphereVertexCount =
		(kSphereSubdivision + 1) * (kSphereSubdivision + 1);

	// 球は1つの四角形を三角形2枚で作るので、1マスあたり6インデックスになる
	static constexpr uint32_t kSphereIndexCount =
		kSphereSubdivision * kSphereSubdivision * 6;

	// Spriteは矩形なので頂点は4つ
	static constexpr uint32_t kSpriteVertexCount = 4;

	// Spriteは三角形2枚で作るので、インデックスは6つ
	static constexpr uint32_t kSpriteIndexCount = 6;

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
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
		ID3D12DescriptorHeap* descriptorHeap,
		UINT descriptorSize,
		UINT index
	) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
		ID3D12DescriptorHeap* descriptorHeap,
		UINT descriptorSize,
		UINT index
	) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUDescriptorHandle(UINT index) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(UINT index) const;
	ID3D12Resource* CreateBufferResource(size_t sizeInBytes);
	bool CreateVertexResource();
	bool CreateIndexResource();
	bool CreateSpriteResource();
	bool CreateSpriteIndexResource();
	bool CreateMaterialResource();
	bool CreateMaterialResourceSprite();
	bool CreateTransformationMatrixResource();
	bool CreateSpriteTransformationMatrixResource();
	bool CreateDirectionalLightResource();
	void CreateViewportAndScissor();

	IDxcBlob* CompileShader(
		const std::wstring& filePath,
		const wchar_t* profile,
		IDxcUtils* dxcUtils,
		IDxcCompiler3* dxcCompiler,
		IDxcIncludeHandler* includeHandler
	);

	void UpdateTransformationMatrix();

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

	ID3D12Resource* indexResource_ = nullptr;
	D3D12_INDEX_BUFFER_VIEW indexBufferView_ = {};

	ID3D12Resource* vertexResourceSprite_ = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite_ = {};

	ID3D12Resource* indexResourceSprite_ = nullptr;
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite_ = {};

	ID3D12Resource* materialResource_ = nullptr;
	Material* materialData_ = nullptr;

	ID3D12Resource* materialResourceSprite_ = nullptr;
	Material* materialDataSprite_ = nullptr;

	ID3D12Resource* transformationMatrixResource_ = nullptr;
	TransformationMatrix* transformationMatrixData_ = nullptr;

	ID3D12Resource* transformationMatrixResourceSprite_ = nullptr;
	TransformationMatrix* transformationMatrixDataSprite_ = nullptr;

	ID3D12Resource* directionalLightResource_ = nullptr;
	DirectionalLight* directionalLightData_ = nullptr;

	TextureManager textureManager_;
	TextureManager textureManagerMonsterBall_;
	DebugGui debugGui_;

	// 球にMonsterBallTextureを使うかどうか
	bool useMonsterBall_ = true;

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

	// Sprite用のUV座標変換の初期値
	Transform uvTransform_ = {
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f },
	};

	// カメラの座標変換行列の初期値
	Transform cameraTransform_ = {
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, -10.0f },
	};

	D3D12_VIEWPORT viewport_ = {};
	D3D12_RECT scissorRect_ = {};
};
