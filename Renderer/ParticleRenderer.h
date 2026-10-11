#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <random>

#include "Camera/ICamera.h"
#include "Graphics/BlendMode.h"

namespace Homura {

/// <summary>
/// インスタンシングでパーティクルを描画する
/// </summary>
class ParticleRenderer {
public:
	enum class DemoMode { Static, Simulation };

	struct Emitter {
		Transform transform{ { 1.0f, 1.0f, 1.0f }, {}, {} };
		uint32_t count = 3;
		float frequency = 0.5f;
		float frequencyTime = 0.0f;
		bool enabled = true;
	};

	struct AABB {
		Vector3 min{ -1.0f, -1.0f, -1.0f };
		Vector3 max{ 1.0f, 1.0f, 1.0f };
	};

	struct AccelerationField {
		Vector3 acceleration{ 15.0f, 0.0f, 0.0f };
		AABB area;
		bool enabled = false;
	};

	static constexpr uint32_t kMaxParticles = 1000;

	ParticleRenderer() = default;
	~ParticleRenderer();
	ParticleRenderer(const ParticleRenderer&) = delete;
	ParticleRenderer& operator=(const ParticleRenderer&) = delete;

	// TextureのSRVは呼び出し元のものを共有する
	bool Initialize(
		const Microsoft::WRL::ComPtr<ID3D12Device>& device,
		D3D12_CPU_DESCRIPTOR_HANDLE instanceSrvCPU,
		D3D12_GPU_DESCRIPTOR_HANDLE instanceSrvGPU,
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvGPU
	);
	void Finalize();
	void Update(float deltaTime, const ICamera& camera);
	void Draw(ID3D12GraphicsCommandList* commandList);
	void DrawDebugGui();

	// 発生位置の周囲へ指定数だけ追加する
	void Emit(uint32_t count);
	void Reset();
	void SetDemoMode(DemoMode mode);
	DemoMode GetDemoMode() const { return demoMode_; }
	uint32_t GetParticleCount() const { return static_cast<uint32_t>(particles_.size()); }
	void SetBillboardEnabled(bool enabled) { useBillboard_ = enabled; }
	void SetBlendMode(BlendMode mode);
	void SetTexture(D3D12_GPU_DESCRIPTOR_HANDLE textureSrvGPU);
	Emitter& GetEmitter() { return emitter_; }
	AccelerationField& GetAccelerationField() { return accelerationField_; }

private:
	struct Particle {
		Transform transform;
		Vector3 velocity;
		Vector4 color;
		float lifeTime;
		float currentTime;
	};

	struct ParticleForGPU {
		Matrix4x4 WVP;
		Matrix4x4 World;
		Vector4 color;
	};

	struct Vertex {
		Vector4 position;
		Vector2 texcoord;
	};

	bool CreatePipelineStates();
	bool CreateResources(D3D12_CPU_DESCRIPTOR_HANDLE instanceSrvCPU);
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);
	Particle MakeParticle();
	void UpdateSimulation(float deltaTime);
	void UpdateInstanceData(const ICamera& camera);
	static bool IsInside(const AABB& area, const Vector3& position);

private:
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::Count)> pipelineStates_;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> instanceResource_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	D3D12_GPU_DESCRIPTOR_HANDLE instanceSrvGPU_{};
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvGPU_{};
	ParticleForGPU* instanceData_ = nullptr;
	uint32_t instanceCount_ = 0;

	std::list<Particle> particles_;
	std::mt19937 randomEngine_{ std::random_device{}() };
	Emitter emitter_;
	AccelerationField accelerationField_;
	DemoMode demoMode_ = DemoMode::Simulation;
	BlendMode blendMode_ = BlendMode::Add;
	bool useBillboard_ = true;
};

} // namespace Homura
