#include "FrameRenderer.h"

#include <algorithm>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

#include "Input/Input.h"
#include "Model/ModelLoader.h"
#include "Model/Primitive.h"
#include "WinApp/WinApp.h"

namespace Homura {

bool FrameRenderer::Initialize(WinApp* winApp) {
	Finalize();
	if (!d3d12Context_.Initialize(winApp)) {
		return false;
	}
	const float aspectRatio = float(WinApp::kClientWidth) / float(WinApp::kClientHeight);
	defaultCamera_.Initialize(aspectRatio);
	debugCamera_.Initialize(aspectRatio);
	if (!CreateRenderers() || !CreateDebugGui(winApp->GetHwnd())) {
		Finalize();
		return false;
	}
	initialized_ = true;
	SetDemoScene(DemoScene::Blend);
	previousFrame_ = std::chrono::steady_clock::now();
	return true;
}

void FrameRenderer::Finalize() {
	// 描画用Resourceを解放する前にGPUの処理完了を待つ
	d3d12Context_.WaitForGpu();
	debugGui_.Finalize();
	particles_.Finalize();
	for (auto& object : objects_) {
		object.Finalize();
	}
	for (auto& resource : intermediateResources_) {
		resource.Reset();
	}
	particleTexture_.Finalize();
	checkerTexture_.Finalize();
	particleIntermediate_.Reset();
	checkerIntermediate_.Reset();
	d3d12Context_.Finalize();
	initialized_ = false;
}

void FrameRenderer::Update(const Input& input) {
	const auto now = std::chrono::steady_clock::now();
	// ウィンドウを止めた後に、粒子が一度で遠くへ飛ばないようにする
	deltaTime_ = std::clamp(std::chrono::duration<float>(now - previousFrame_).count(), 0.0f, 0.1f);
	previousFrame_ = now;
	if (input.IsTriggerKey(DIK_F1)) {
		useDebugCamera_ = !useDebugCamera_;
	}
	const BYTE sceneKeys[]{ DIK_1, DIK_2, DIK_3, DIK_4, DIK_5, DIK_6, DIK_7, DIK_8 };
#ifdef USE_IMGUI
	const bool editingText = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
	const bool operatingGui = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
#else
	const bool editingText = false;
	const bool operatingGui = false;
#endif
	if (!editingText) {
		for (size_t i = 0; i < _countof(sceneKeys); ++i) {
			if (input.IsTriggerKey(sceneKeys[i])) {
				SetDemoScene(static_cast<DemoScene>(i));
			}
		}
	}
	// 数値入力や操作画面のドラッグ中はカメラを動かさない
	if (useDebugCamera_ && !editingText && !operatingGui) {
		debugCamera_.Update(input);
	}
}

bool FrameRenderer::Draw() {
	if (!initialized_ || !d3d12Context_.BeginFrame()) {
		return false;
	}
	debugGui_.BeginFrame();
	DrawDebugGui();
	debugGui_.EndFrame();
	defaultCamera_.UpdateMatrix();
	for (auto& object : objects_) {
		object.UpdateTransformationMatrix(GetActiveCamera());
	}
	d3d12Context_.SetShaderVisibleDescriptorHeap();
	DrawObjects();
	if (demoScene_ == DemoScene::Instancing || demoScene_ == DemoScene::Particle) {
		particles_.SetTexture(particles_.GetDemoMode() == ParticleRenderer::DemoMode::Static ?
			checkerTexture_.GetTextureSrvHandleGPU() : particleTexture_.GetTextureSrvHandleGPU());
		particles_.Update(paused_ ? 0.0f : deltaTime_, GetActiveCamera());
		// 不透明な物体を描いてから、Depthを書かない粒子を重ねる
		particles_.Draw(d3d12Context_.GetCommandList().Get());
	}
	debugGui_.Render(d3d12Context_.GetCommandList().Get());
	return d3d12Context_.EndFrame();
}

bool FrameRenderer::CreateRenderers() {
	const ModelData plane = LoadModelFile("Resources", "plane.obj");
	const ModelData fence = LoadModelFile("Resources/CG3/fence", "fence.obj");
	const ModelData gltf = LoadModelFile("Resources/CG3", "plane.gltf");
	const ModelData sphere = MakeSphereModel("Resources/CG3/white.png");
	if (plane.vertices.empty() || fence.vertices.empty() || gltf.vertices.empty()) {
		return false;
	}
	const ModelData* models[]{ &plane, &plane, &fence, &sphere, &plane, &gltf };
	if (!d3d12Context_.ResetCommandList()) {
		return false;
	}
	for (size_t i = 0; i < objects_.size(); ++i) {
		const UINT descriptor = kObjectTextureSRVIndex + static_cast<UINT>(i);
		if (!objects_[i].Initialize(d3d12Context_.GetDevice(), d3d12Context_.GetCommandList(),
			d3d12Context_.GetSRVCPUDescriptorHandle(descriptor), d3d12Context_.GetSRVGPUDescriptorHandle(descriptor),
			&intermediateResources_[i], *models[i])) {
			return false;
		}
	}
	if (!particleTexture_.Initialize(d3d12Context_.GetDevice(), d3d12Context_.GetCommandList(),
		"Resources/CG3/circle.png", d3d12Context_.GetSRVCPUDescriptorHandle(kParticleTextureSRVIndex),
		d3d12Context_.GetSRVGPUDescriptorHandle(kParticleTextureSRVIndex), &particleIntermediate_) ||
		!checkerTexture_.Initialize(d3d12Context_.GetDevice(), d3d12Context_.GetCommandList(),
			"Resources/uvChecker.png", d3d12Context_.GetSRVCPUDescriptorHandle(kModelTextureSRVIndex),
			d3d12Context_.GetSRVGPUDescriptorHandle(kModelTextureSRVIndex), &checkerIntermediate_) ||
		!particles_.Initialize(d3d12Context_.GetDevice(),
			d3d12Context_.GetSRVCPUDescriptorHandle(kParticleInstanceSRVIndex),
			d3d12Context_.GetSRVGPUDescriptorHandle(kParticleInstanceSRVIndex),
			particleTexture_.GetTextureSrvHandleGPU())) {
		return false;
	}
	if (!d3d12Context_.ExecuteCommandListAndWait()) {
		return false;
	}
	for (auto& resource : intermediateResources_) {
		resource.Reset();
	}
	particleIntermediate_.Reset();
	checkerIntermediate_.Reset();
	return true;
}

void FrameRenderer::SetDemoScene(DemoScene scene) {
	if (!initialized_ || scene < DemoScene::Blend || scene >= DemoScene::Count) {
		return;
	}
	demoScene_ = scene;
	paused_ = false;
	useDebugCamera_ = false;
	defaultCamera_.GetTransform() = { { 1.0f, 1.0f, 1.0f }, {}, { 0.0f, 0.0f, -10.0f } };
	for (auto& object : objects_) {
		object.GetTransform() = { { 1.0f, 1.0f, 1.0f }, {}, {} };
		object.SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		object.SetBlendMode(BlendMode::None);
		object.SetAlphaCutoff(0.5f);
		object.SetLightingEnabled(false);
		object.SetShadingModel(ShadingModel::BlinnPhong);
		object.SetShininess(40.0f);
		object.SetDirectionalLight({ 1.0f, 1.0f, 1.0f, 1.0f }, { -0.5f, -0.6f, 1.0f }, 1.0f);
		PointLight point;
		point.intensity = 0.0f;
		object.SetPointLight(point);
		SpotLight spot;
		spot.intensity = 0.0f;
		object.SetSpotLight(spot);
	}
	objects_[kBackground].GetTransform() = { { 2.6f, 1.7f, 1.0f }, {}, { 0.75f, 0.0f, 2.0f } };
	objects_[kBackground].SetColor({ 0.45f, 0.55f, 0.8f, 1.0f });
	objects_[kBlendPlane].GetTransform().translate = { 0.75f, 0.0f, 0.0f };
	objects_[kBlendPlane].SetColor({ 1.0f, 0.45f, 0.2f, 0.5f });
	objects_[kBlendPlane].SetBlendMode(BlendMode::Normal);
	objects_[kFence].GetTransform().translate = { 0.75f, 0.0f, 0.0f };
	objects_[kSphere].GetTransform().translate = { 0.75f, 0.0f, 0.0f };
	objects_[kSphere].SetLightingEnabled(true);
	objects_[kSphere].SetColor({ 0.65f, 0.7f, 0.85f, 1.0f });
	objects_[kObj].GetTransform().translate = { -0.75f, 0.0f, 0.0f };
	objects_[kGltf].GetTransform().translate = { 1.75f, 0.0f, 0.0f };
	if (scene == DemoScene::Specular) {
		objects_[kSphere].GetTransform().scale = { 1.0f, 1.5f, 0.65f };
	} else if (scene == DemoScene::Point || scene == DemoScene::Spot) {
		objects_[kSphere].SetDirectionalLight({ 1, 1, 1, 1 }, { 0, -1, 0 }, 0.0f);
		if (scene == DemoScene::Point) {
			PointLight point;
			point.position = { 1.3f, 1.5f, -2.0f };
			point.intensity = 3.0f;
			point.radius = 6.0f;
			point.decay = 2.0f;
			objects_[kSphere].SetPointLight(point);
		} else {
			SpotLight spot;
			spot.position = { 1.8f, 1.5f, -3.0f };
			spot.direction = Normalize({ -1.05f, -1.5f, 3.0f });
			spot.intensity = 5.0f;
			spot.distance = 7.0f;
			spot.decay = 2.0f;
			spot.cosAngle = 0.8f;
			spot.cosFalloffStart = 0.95f;
			objects_[kSphere].SetSpotLight(spot);
		}
	}
	if (scene == DemoScene::Instancing || scene == DemoScene::Particle) {
		// 操作画面に隠れないよう、10枚の面を少し引いて見る
		defaultCamera_.GetTransform().translate = scene == DemoScene::Instancing ?
			Vector3{ -1.3f, 0.0f, -14.0f } : Vector3{ -0.8f, 0.0f, -12.0f };
		objects_[kBackground].SetColor({ 0.06f, 0.08f, 0.1f, 1.0f });
		particles_.GetEmitter() = ParticleRenderer::Emitter{};
		particles_.GetAccelerationField() = ParticleRenderer::AccelerationField{};
		particles_.SetBillboardEnabled(true);
		particles_.SetBlendMode(scene == DemoScene::Instancing ? BlendMode::Normal : BlendMode::Add);
		particles_.SetDemoMode(scene == DemoScene::Instancing ? ParticleRenderer::DemoMode::Static : ParticleRenderer::DemoMode::Simulation);
		particles_.Reset();
	}
}

FrameRenderer::DemoScene FrameRenderer::GetDemoScene() const {
	return demoScene_;
}

void FrameRenderer::DrawObjects() {
	auto* commandList = d3d12Context_.GetCommandList().Get();
	switch (demoScene_) {
	case DemoScene::Blend:
		objects_[kBackground].Draw(commandList);
		objects_[kBlendPlane].Draw(commandList);
		break;
	case DemoScene::AlphaTest:
		objects_[kBackground].Draw(commandList);
		objects_[kFence].Draw(commandList);
		break;
	case DemoScene::Particle:
		objects_[kBackground].Draw(commandList);
		break;
	case DemoScene::Specular:
	case DemoScene::Point:
	case DemoScene::Spot:
		objects_[kSphere].Draw(commandList);
		break;
	case DemoScene::Model:
		objects_[kObj].Draw(commandList);
		objects_[kGltf].Draw(commandList);
		break;
	default:
		break;
	}
}

bool FrameRenderer::CreateDebugGui(HWND hwnd) {
	return debugGui_.Initialize(hwnd, d3d12Context_.GetDevice().Get(), 2,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, d3d12Context_.GetSRVDescriptorHeap().Get(),
		d3d12Context_.GetSRVCPUDescriptorHandle(kImGuiSRVIndex),
		d3d12Context_.GetSRVGPUDescriptorHandle(kImGuiSRVIndex));
}

void FrameRenderer::DrawDebugGui() {
#ifdef USE_IMGUI
	ImGui::SetNextWindowPos({ 12.0f, 12.0f }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize({ 330.0f, 690.0f }, ImGuiCond_FirstUseEver);
	ImGui::Begin("CG3 Settings");
	const char* sceneNames[]{ "00_01-02 Blend", "00_03 Alpha test", "01_00 Instancing", "01_01-04 Particle",
		"02_00-02 Specular", "03_00 Point light", "03_01 Spot light", "04_00-01 Assimp" };
	int scene = static_cast<int>(demoScene_);
	if (ImGui::Combo("Chapter", &scene, sceneNames, _countof(sceneNames))) {
		SetDemoScene(static_cast<DemoScene>(scene));
	}
	ImGui::Text("1-8: Chapter / F1: Debug camera");
	if (ImGui::Button("Reset chapter")) {
		SetDemoScene(demoScene_);
	}
	ImGui::Separator();
	if (demoScene_ == DemoScene::Instancing || demoScene_ == DemoScene::Particle) {
		ImGui::Checkbox("Pause", &paused_);
		particles_.DrawDebugGui();
	} else if (demoScene_ == DemoScene::Model) {
		ImGui::TextWrapped("Left: OBJ / Right: glTF (root rotation + child translation)");
		if (ImGui::CollapsingHeader("OBJ")) { objects_[kObj].DrawDebugGui(); }
		if (ImGui::CollapsingHeader("glTF")) { objects_[kGltf].DrawDebugGui(); }
	} else {
		size_t selected = demoScene_ == DemoScene::Blend ? kBlendPlane :
			(demoScene_ == DemoScene::AlphaTest ? kFence : kSphere);
		objects_[selected].DrawDebugGui();
	}
	ImGui::Separator();
	if (ImGui::CollapsingHeader("Camera")) {
		ImGui::Checkbox("UseDebugCamera(F1)", &useDebugCamera_);
		if (useDebugCamera_) {
			ImGui::TextWrapped("RightDrag:Look  WASD/QE:Move  ZC:Roll  Shift:Fast  R:Reset");
		} else {
			auto& transform = defaultCamera_.GetTransform();
			ImGui::DragFloat3("Camera position", &transform.translate.x, 0.02f);
			ImGui::DragFloat3("Camera rotation", &transform.rotate.x, 0.01f);
		}
	}
	ImGui::End();
#endif
}

const ICamera& FrameRenderer::GetActiveCamera() const {
	return useDebugCamera_ ? static_cast<const ICamera&>(debugCamera_) : defaultCamera_;
}

} // namespace Homura
