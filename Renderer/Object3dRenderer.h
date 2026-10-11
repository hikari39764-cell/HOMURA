#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>

#include "Graphics/BlendMode.h"
#include "Math/MathUtil.h"
#include "Model/ModelData.h"
#include "Renderer/IRenderable.h"
#include "Texture/TextureManager.h"

namespace Homura {

enum class ShadingModel {
	HalfLambert,
	Phong,
	BlinnPhong
};

struct PointLight {
	Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
	Vector3 position = { 0.0f, 2.0f, 0.0f };
	float intensity = 1.0f;
	float radius = 10.0f;
	float decay = 1.0f;
	float padding[2]{};
};

static_assert(sizeof(PointLight) == 48);

struct SpotLight {
	Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
	Vector3 position = { 2.0f, 1.25f, 0.0f };
	float intensity = 4.0f;
	Vector3 direction = { -0.70710678f, -0.70710678f, 0.0f };
	float distance = 7.0f;
	float decay = 2.0f;
	float cosAngle = 0.5f;
	float cosFalloffStart = 0.8660254f;
	float padding = 0.0f;
};

static_assert(sizeof(SpotLight) == 64);

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

	Transform& GetTransform();
	const Transform& GetTransform() const;
	void SetBlendMode(BlendMode blendMode);
	BlendMode GetBlendMode() const;
	void SetColor(const Vector4& color);
	void SetMaterialColor(const Vector4& color);
	void SetAlphaCutoff(float alphaCutoff);
	void SetLightingEnabled(bool enabled);
	void SetUVTransform(const Matrix4x4& uvTransform);
	void SetShadingModel(ShadingModel shadingModel);
	ShadingModel GetShadingModel() const;
	void SetShininess(float shininess);
	void SetDirectionalLight(const Vector4& color, const Vector3& direction, float intensity);
	void SetPointLight(const PointLight& light);
	PointLight GetPointLight() const;
	void SetSpotLight(const SpotLight& light);
	SpotLight GetSpotLight() const;

private:
	struct Material {
		Vector4 color;
		int32_t enableLighting;
		float alphaCutoff;
		float shininess;
		int32_t shadingModel;
		Matrix4x4 uvTransform;
	};

	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
		Matrix4x4 WorldInverseTranspose;
	};

	struct CameraData {
		Vector3 worldPosition;
		float padding;
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
	bool CreateCameraResource();
	bool CreatePointLightResource();
	bool CreateSpotLightResource();

private:
	Microsoft::WRL::ComPtr<ID3D12Device> device_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::Count)> graphicsPipelineStates_;
	BlendMode blendMode_ = BlendMode::None;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_ = {};

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Material* materialData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
	TransformationMatrix* transformationMatrixData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
	DirectionalLight* directionalLightData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
	CameraData* cameraData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
	PointLight* pointLightData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
	SpotLight* spotLightData_ = nullptr;

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
