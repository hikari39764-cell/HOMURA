#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "Math/MathUtil.h"
#include "Model/ModelData.h"
#include "Renderer/IRenderable.h"
#include "Texture/TextureManager.h"

namespace Homura {

class Object3dRenderer : public IRenderable {
public:
	bool Initialize(
		const Microsoft::WRL::ComPtr<ID3D12Device>& device,
		const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList,
		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU,
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU,
		Microsoft::WRL::ComPtr<ID3D12Resource>* textureIntermediateResource,
		const ModelData& modelData
	);
	void Finalize();

	void UpdateTransformationMatrix(const ICamera& camera) override;
	void DrawDebugGui();
	void Draw(ID3D12GraphicsCommandList* commandList) override;

private:
	struct Material {
		Vector4 color;
		int32_t enableLighting;
		float padding[3];
		Matrix4x4 uvTransform;
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
	bool CreateTexture(
		const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList,
		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU,
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU,
		Microsoft::WRL::ComPtr<ID3D12Resource>* intermediateResource
	);
	bool CreateGraphicsPipelineState();
	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
		const std::wstring& filePath,
		const wchar_t* profile,
		const Microsoft::WRL::ComPtr<IDxcUtils>& dxcUtils,
		const Microsoft::WRL::ComPtr<IDxcCompiler3>& dxcCompiler,
		const Microsoft::WRL::ComPtr<IDxcIncludeHandler>& includeHandler
	);
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);
	bool CreateVertexResource();
	bool CreateMaterialResource();
	bool CreateTransformationMatrixResource();
	bool CreateDirectionalLightResource();

private:
	Microsoft::WRL::ComPtr<ID3D12Device> device_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_ = {};

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Material* materialData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
	TransformationMatrix* transformationMatrixData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
	DirectionalLight* directionalLightData_ = nullptr;

	TextureManager textureManagerModel_;
	ModelData modelData_;

	// 3Dモデル用の座標変換行列の初期値
	Transform transform_ = {
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f },
	};
};

} // namespace Homura
