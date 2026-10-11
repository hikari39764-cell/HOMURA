#include "ParticleRenderer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <dxcapi.h>
#include <numbers>
#include <vector>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

#include "DebugTools/Logger/Logger.h"

#pragma comment(lib, "dxcompiler.lib")

namespace Homura {

namespace {

Microsoft::WRL::ComPtr<IDxcBlob> CompileParticleShader(const wchar_t* path, const wchar_t* profile) {
	Microsoft::WRL::ComPtr<IDxcUtils> utils;
	Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
	if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) ||
		FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))) ||
		FAILED(utils->CreateDefaultIncludeHandler(&includeHandler))) {
		return {};
	}

	Microsoft::WRL::ComPtr<IDxcBlobEncoding> source;
	if (FAILED(utils->LoadFile(path, nullptr, &source))) {
		Log(L"Failed to load particle shader.");
		return {};
	}
	DxcBuffer buffer{ source->GetBufferPointer(), source->GetBufferSize(), DXC_CP_UTF8 };
	LPCWSTR arguments[]{ path, L"-E", L"main", L"-T", profile, L"-Zi", L"-Qembed_debug", L"-Od", L"-Zpr" };
	Microsoft::WRL::ComPtr<IDxcResult> result;
	if (FAILED(compiler->Compile(&buffer, arguments, _countof(arguments), includeHandler.Get(), IID_PPV_ARGS(&result)))) {
		return {};
	}
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
	result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
	if (errors != nullptr && errors->GetStringLength() != 0) {
		Log(errors->GetStringPointer());
		return {};
	}
	HRESULT status = E_FAIL;
	if (FAILED(result->GetStatus(&status)) || FAILED(status)) {
		return {};
	}
	Microsoft::WRL::ComPtr<IDxcBlob> shader;
	if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader), nullptr))) {
		return {};
	}
	return shader;
}

} // namespace

ParticleRenderer::~ParticleRenderer() {
	Finalize();
}

bool ParticleRenderer::Initialize(
	const Microsoft::WRL::ComPtr<ID3D12Device>& device,
	D3D12_CPU_DESCRIPTOR_HANDLE instanceSrvCPU,
	D3D12_GPU_DESCRIPTOR_HANDLE instanceSrvGPU,
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvGPU
) {
	assert(device != nullptr);
	if (device == nullptr || instanceSrvCPU.ptr == 0 || instanceSrvGPU.ptr == 0 || textureSrvGPU.ptr == 0) {
		return false;
	}
	Finalize();
	device_ = device;
	instanceSrvGPU_ = instanceSrvGPU;
	textureSrvGPU_ = textureSrvGPU;
	if (!CreatePipelineStates() || !CreateResources(instanceSrvCPU)) {
		Finalize();
		return false;
	}
	Reset();
	return true;
}

void ParticleRenderer::Finalize() {
	// GPUの完了待ちは呼び出し元で行う
	instanceData_ = nullptr;
	instanceCount_ = 0;
	particles_.clear();
	instanceResource_.Reset();
	vertexResource_.Reset();
	for (auto& pipeline : pipelineStates_) {
		pipeline.Reset();
	}
	rootSignature_.Reset();
	device_.Reset();
	instanceSrvGPU_ = {};
	textureSrvGPU_ = {};
	vertexBufferView_ = {};
	emitter_.frequencyTime = 0.0f;
}

Microsoft::WRL::ComPtr<ID3D12Resource> ParticleRenderer::CreateBufferResource(size_t sizeInBytes) {
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = sizeInBytes;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource)))) {
		return {};
	}
	return resource;
}

bool ParticleRenderer::CreateResources(D3D12_CPU_DESCRIPTOR_HANDLE instanceSrvCPU) {
	// 1枚の面を全インスタンスで共有する
	const Vertex vertices[]{
		{ { -0.5f, 0.5f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
		{ { 0.5f, 0.5f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
		{ { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
		{ { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
		{ { 0.5f, 0.5f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
		{ { 0.5f, -0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f } }
	};
	vertexResource_ = CreateBufferResource(sizeof(vertices));
	instanceResource_ = CreateBufferResource(sizeof(ParticleForGPU) * kMaxParticles);
	if (vertexResource_ == nullptr || instanceResource_ == nullptr) {
		return false;
	}
	D3D12_RANGE noRead{ 0, 0 };
	void* mapped = nullptr;
	if (FAILED(vertexResource_->Map(0, &noRead, &mapped))) {
		return false;
	}
	std::memcpy(mapped, vertices, sizeof(vertices));
	vertexResource_->Unmap(0, nullptr);
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = sizeof(vertices);
	vertexBufferView_.StrideInBytes = sizeof(Vertex);
	if (FAILED(instanceResource_->Map(0, &noRead, reinterpret_cast<void**>(&instanceData_)))) {
		return false;
	}

	// StructuredBufferの要素数とCPU側の構造体サイズを合わせる
	static_assert(sizeof(ParticleForGPU) == 144);
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = DXGI_FORMAT_UNKNOWN;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srv.Buffer.NumElements = kMaxParticles;
	srv.Buffer.StructureByteStride = sizeof(ParticleForGPU);
	device_->CreateShaderResourceView(instanceResource_.Get(), &srv, instanceSrvCPU);
	return true;
}

bool ParticleRenderer::CreatePipelineStates() {
	auto vertexShader = CompileParticleShader(L"Shaders/Particle.VS.hlsl", L"vs_6_0");
	auto pixelShader = CompileParticleShader(L"Shaders/Particle.PS.hlsl", L"ps_6_0");
	if (vertexShader == nullptr || pixelShader == nullptr) {
		return false;
	}

	// VertexShaderのインスタンスとPixelShaderのTextureを別々に渡す
	D3D12_DESCRIPTOR_RANGE ranges[2]{};
	D3D12_ROOT_PARAMETER parameters[2]{};
	for (UINT i = 0; i < 2; ++i) {
		ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[i].NumDescriptors = 1;
		ranges[i].BaseShaderRegister = 0;
		ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[i].ShaderVisibility = i == 0 ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_PIXEL;
		parameters[i].DescriptorTable.NumDescriptorRanges = 1;
		parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
	}
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC rootDesc{};
	rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootDesc.NumParameters = _countof(parameters);
	rootDesc.pParameters = parameters;
	rootDesc.NumStaticSamplers = 1;
	rootDesc.pStaticSamplers = &sampler;
	Microsoft::WRL::ComPtr<ID3DBlob> signature;
	Microsoft::WRL::ComPtr<ID3DBlob> errors;
	if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors))) {
		if (errors != nullptr) {
			Log(static_cast<const char*>(errors->GetBufferPointer()));
		}
		return false;
	}
	if (FAILED(device_->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature_)))) {
		return false;
	}
	D3D12_INPUT_ELEMENT_DESC inputElements[]{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();
	desc.InputLayout = { inputElements, _countof(inputElements) };
	desc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
	desc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.RasterizerState.DepthClipEnable = TRUE;
	// 不透明な物体との前後関係は調べ、粒子自身はDepthを書き込まない
	desc.DepthStencilState.DepthEnable = TRUE;
	desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	for (size_t i = 0; i < pipelineStates_.size(); ++i) {
		desc.BlendState = MakeBlendDesc(static_cast<BlendMode>(i));
		if (FAILED(device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineStates_[i])))) {
			return false;
		}
	}
	return true;
}

ParticleRenderer::Particle ParticleRenderer::MakeParticle() {
	std::uniform_real_distribution<float> offset(-1.0f, 1.0f);
	std::uniform_real_distribution<float> color(0.0f, 1.0f);
	std::uniform_real_distribution<float> life(1.0f, 3.0f);
	Particle particle{};
	particle.transform.scale = { 0.5f, 0.5f, 0.5f };
	const Vector3 local{ offset(randomEngine_), offset(randomEngine_), offset(randomEngine_) };
	const Matrix4x4 emitterMatrix = MakeAffineMatrix(emitter_.transform.scale, emitter_.transform.rotate, emitter_.transform.translate);
	particle.transform.translate = {
		local.x * emitterMatrix.m[0][0] + local.y * emitterMatrix.m[1][0] + local.z * emitterMatrix.m[2][0] + emitterMatrix.m[3][0],
		local.x * emitterMatrix.m[0][1] + local.y * emitterMatrix.m[1][1] + local.z * emitterMatrix.m[2][1] + emitterMatrix.m[3][1],
		local.x * emitterMatrix.m[0][2] + local.y * emitterMatrix.m[1][2] + local.z * emitterMatrix.m[2][2] + emitterMatrix.m[3][2]
	};
	particle.velocity = { offset(randomEngine_), offset(randomEngine_), offset(randomEngine_) };
	particle.color = { color(randomEngine_), color(randomEngine_), color(randomEngine_), 1.0f };
	particle.lifeTime = life(randomEngine_);
	return particle;
}

void ParticleRenderer::Emit(uint32_t count) {
	const size_t available = kMaxParticles - particles_.size();
	const size_t emitCount = (std::min)(static_cast<size_t>(count), available);
	for (size_t i = 0; i < emitCount; ++i) {
		particles_.push_back(MakeParticle());
	}
}

void ParticleRenderer::Reset() {
	particles_.clear();
	instanceCount_ = 0;
	emitter_.frequencyTime = 0.0f;
	if (demoMode_ == DemoMode::Static) {
		// 10枚の面でインスタンシングを確認する
		for (uint32_t i = 0; i < 10; ++i) {
			Particle particle = MakeParticle();
			particle.transform.scale = { 1.0f, 1.0f, 1.0f };
			particle.transform.translate = { (static_cast<float>(i % 5) - 2.0f) * 1.4f, (static_cast<float>(i / 5) - 0.5f) * 1.4f, 0.0f };
			particle.velocity = {};
			particles_.push_back(particle);
		}
	} else {
		Emit(emitter_.count);
	}
}

void ParticleRenderer::SetDemoMode(DemoMode mode) {
	if (demoMode_ != mode) {
		demoMode_ = mode;
		Reset();
	}
}

void ParticleRenderer::SetBlendMode(BlendMode mode) {
	if (static_cast<size_t>(mode) < pipelineStates_.size()) {
		blendMode_ = mode;
	}
}

void ParticleRenderer::SetTexture(D3D12_GPU_DESCRIPTOR_HANDLE textureSrvGPU) {
	if (textureSrvGPU.ptr != 0) {
		textureSrvGPU_ = textureSrvGPU;
	}
}

bool ParticleRenderer::IsInside(const AABB& area, const Vector3& position) {
	return position.x >= area.min.x && position.x <= area.max.x &&
		position.y >= area.min.y && position.y <= area.max.y &&
		position.z >= area.min.z && position.z <= area.max.z;
}

void ParticleRenderer::UpdateSimulation(float deltaTime) {
	for (auto it = particles_.begin(); it != particles_.end();) {
		it->currentTime += deltaTime;
		if (it->currentTime >= it->lifeTime) {
			it = particles_.erase(it);
			continue;
		}
		if (accelerationField_.enabled && IsInside(accelerationField_.area, it->transform.translate)) {
			it->velocity.x += accelerationField_.acceleration.x * deltaTime;
			it->velocity.y += accelerationField_.acceleration.y * deltaTime;
			it->velocity.z += accelerationField_.acceleration.z * deltaTime;
		}
		it->transform.translate.x += it->velocity.x * deltaTime;
		it->transform.translate.y += it->velocity.y * deltaTime;
		it->transform.translate.z += it->velocity.z * deltaTime;
		++it;
	}
	if (emitter_.enabled && std::isfinite(emitter_.frequency) && emitter_.frequency > 0.0f) {
		if (!std::isfinite(emitter_.frequencyTime)) {
			emitter_.frequencyTime = 0.0f;
		}
		const double elapsed = static_cast<double>(emitter_.frequencyTime) + deltaTime;
		const double batches = std::floor(elapsed / emitter_.frequency);
		if (batches > 0.0) {
			const double count = (std::min)(batches * emitter_.count, static_cast<double>(kMaxParticles));
			Emit(static_cast<uint32_t>(count));
		}
		emitter_.frequencyTime = static_cast<float>(std::fmod(elapsed, emitter_.frequency));
	}
}

void ParticleRenderer::Update(float deltaTime, const ICamera& camera) {
	if (instanceData_ == nullptr) {
		return;
	}
	if (demoMode_ == DemoMode::Simulation && std::isfinite(deltaTime) && deltaTime > 0.0f) {
		UpdateSimulation(deltaTime);
	}
	UpdateInstanceData(camera);
}

void ParticleRenderer::UpdateInstanceData(const ICamera& camera) {
	Matrix4x4 cameraRotation = Inverse(camera.GetViewMatrix());
	cameraRotation.m[3][0] = 0.0f;
	cameraRotation.m[3][1] = 0.0f;
	cameraRotation.m[3][2] = 0.0f;
	const Matrix4x4 billboard = Multiply(MakeRotateYMatrix(std::numbers::pi_v<float>), cameraRotation);
	const Matrix4x4 viewProjection = Multiply(camera.GetViewMatrix(), camera.GetProjectionMatrix());

	std::vector<const Particle*> drawOrder;
	drawOrder.reserve(particles_.size());
	for (const Particle& particle : particles_) {
		drawOrder.push_back(&particle);
	}
	if (blendMode_ == BlendMode::Normal) {
		// 半透明の面は奥から重ねる
		const Matrix4x4& view = camera.GetViewMatrix();
		std::stable_sort(drawOrder.begin(), drawOrder.end(), [&view](const Particle* a, const Particle* b) {
			const Vector3& pa = a->transform.translate;
			const Vector3& pb = b->transform.translate;
			return pa.x * view.m[0][2] + pa.y * view.m[1][2] + pa.z * view.m[2][2] >
				pb.x * view.m[0][2] + pb.y * view.m[1][2] + pb.z * view.m[2][2];
		});
	}
	instanceCount_ = 0;
	for (const Particle* particle : drawOrder) {
		if (instanceCount_ >= kMaxParticles) {
			break;
		}
		const Transform& transform = particle->transform;
		ParticleForGPU& output = instanceData_[instanceCount_++];
		output.World = useBillboard_ ? Multiply(Multiply(MakeScaleMatrix(transform.scale), billboard), MakeTranslateMatrix(transform.translate)) :
			MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
		output.WVP = Multiply(output.World, viewProjection);
		output.color = particle->color;
		if (demoMode_ == DemoMode::Simulation) {
			output.color.w *= std::clamp(1.0f - particle->currentTime / particle->lifeTime, 0.0f, 1.0f);
		}
	}
}

void ParticleRenderer::Draw(ID3D12GraphicsCommandList* commandList) {
	if (commandList == nullptr || instanceData_ == nullptr || instanceCount_ == 0) {
		return;
	}
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pipelineStates_[static_cast<size_t>(blendMode_)].Get());
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->SetGraphicsRootDescriptorTable(0, instanceSrvGPU_);
	commandList->SetGraphicsRootDescriptorTable(1, textureSrvGPU_);
	commandList->DrawInstanced(6, instanceCount_, 0, 0);
}

void ParticleRenderer::DrawDebugGui() {
#ifdef USE_IMGUI
	if (!ImGui::CollapsingHeader("Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}
	ImGui::PushID("Particles");
	int mode = static_cast<int>(demoMode_);
	const char* demoNames[]{ "10 static planes", "Particle simulation" };
	if (ImGui::Combo("Demo", &mode, demoNames, _countof(demoNames))) {
		SetDemoMode(static_cast<DemoMode>(mode));
	}
	int blend = static_cast<int>(blendMode_);
	if (ImGui::Combo("Blend", &blend, kBlendModeNames, static_cast<int>(BlendMode::Count))) {
		SetBlendMode(static_cast<BlendMode>(blend));
	}
	ImGui::Checkbox("Billboard", &useBillboard_);
	ImGui::Text("Alive: %u / %u", GetParticleCount(), kMaxParticles);
	ImGui::TextWrapped("Billboard faces the camera. Particles fade with age; depth test is enabled and depth writes are disabled.");
	if (ImGui::Button("Reset")) {
		Reset();
	}
	if (demoMode_ == DemoMode::Simulation) {
		ImGui::SameLine();
		if (ImGui::Button("Emit")) {
			Emit(emitter_.count);
		}
		ImGui::Checkbox("Auto emit", &emitter_.enabled);
		ImGui::DragFloat3("Emitter position", &emitter_.transform.translate.x, 0.05f);
		ImGui::DragFloat3("Emitter size", &emitter_.transform.scale.x, 0.05f, 0.0f, 10.0f);
		int count = static_cast<int>((std::min)(emitter_.count, kMaxParticles));
		if (ImGui::DragInt("Emit count", &count, 1.0f, 0, static_cast<int>(kMaxParticles))) {
			emitter_.count = static_cast<uint32_t>(std::clamp(count, 0, static_cast<int>(kMaxParticles)));
		}
		ImGui::DragFloat("Emit interval (seconds)", &emitter_.frequency, 0.01f, 0.01f, 10.0f);
		ImGui::Checkbox("Acceleration field", &accelerationField_.enabled);
		ImGui::DragFloat3("Acceleration", &accelerationField_.acceleration.x, 0.1f);
		ImGui::DragFloat3("Field min", &accelerationField_.area.min.x, 0.05f);
		ImGui::DragFloat3("Field max", &accelerationField_.area.max.x, 0.05f);
		ImGui::TextWrapped("Only particles inside the field AABB receive acceleration.");
	}
	ImGui::PopID();
#endif
}

} // namespace Homura
