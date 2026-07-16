#include "FrameRenderer.h"

#include <cassert>
#include <format>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

#include "DebugTools/Logger/Logger.h"
#include "Input/Input.h"
#include "Model/ModelLoader.h"
#include "WinApp/WinConfig.h"

namespace Homura {

bool FrameRenderer::Initialize(HWND hwnd) {
	if (!d3d12Context_.Initialize(hwnd)) {
		return false;
	}

	const float aspectRatio =
		float(WinConfig::kClientWidth) / float(WinConfig::kClientHeight);
	defaultCamera_.Initialize(aspectRatio);
	debugCamera_.Initialize(aspectRatio);

	if (!CreateObject3dRenderer()) {
		return false;
	}

	if (!CreateDebugGui(hwnd)) {
		return false;
	}

	return true;
}

void FrameRenderer::Finalize() {
	modelIntermediateResource_.Reset();
	debugGui_.Finalize();
	object3dRenderer_.Finalize();
	d3d12Context_.Finalize();
}

void FrameRenderer::Update(const Input& input) {
	if (input.IsTriggerKey(DIK_F1)) {
		useDebugCamera_ = !useDebugCamera_;
	}

	if (useDebugCamera_) {
		debugCamera_.Update(input);
	}
}

void FrameRenderer::Draw() {
	if (!d3d12Context_.BeginFrame()) {
		return;
	}

	debugGui_.BeginFrame();
	DrawDebugGui();
	debugGui_.EndFrame();

	defaultCamera_.UpdateMatrix();
	object3dRenderer_.UpdateTransformationMatrix(GetActiveCamera());

	d3d12Context_.SetShaderVisibleDescriptorHeap();
	object3dRenderer_.Draw(d3d12Context_.GetCommandList().Get());
	debugGui_.Render(d3d12Context_.GetCommandList().Get());

	d3d12Context_.EndFrame();
}

bool FrameRenderer::CreateObject3dRenderer() {
	// ResourcesフォルダのOBJを読み込んでModelDataにする
	modelData_ = LoadObjFile("Resources", "plane.obj");
	assert(!modelData_.vertices.empty());
	assert(!modelData_.material.textureFilePath.empty());

	if (modelData_.vertices.empty() || modelData_.material.textureFilePath.empty()) {
		return false;
	}

	Log(std::format(
		"Complete load model, vertices:{}, texture:{}!!!\n",
		modelData_.vertices.size(),
		modelData_.material.textureFilePath
	));

	if (!d3d12Context_.ResetCommandList()) {
		return false;
	}

	if (!object3dRenderer_.Initialize(
		d3d12Context_.GetDevice(),
		d3d12Context_.GetCommandList(),
		d3d12Context_.GetSRVCPUDescriptorHandle(kModelTextureSRVIndex),
		d3d12Context_.GetSRVGPUDescriptorHandle(kModelTextureSRVIndex),
		&modelIntermediateResource_,
		modelData_
	)) {
		return false;
	}

	return d3d12Context_.ExecuteCommandListAndWait();
}

bool FrameRenderer::CreateDebugGui(HWND hwnd) {
	return debugGui_.Initialize(
		hwnd,
		d3d12Context_.GetDevice().Get(),
		kBackBufferCount,
		kBackBufferFormat,
		d3d12Context_.GetSRVDescriptorHeap().Get(),
		d3d12Context_.GetSRVCPUDescriptorHandle(kImGuiSRVIndex),
		d3d12Context_.GetSRVGPUDescriptorHandle(kImGuiSRVIndex)
	);
}

void FrameRenderer::DrawDebugGui() {
#ifdef USE_IMGUI
	ImGui::Begin("Settings");

	object3dRenderer_.DrawDebugGui();

	ImGui::Separator();
	ImGui::Checkbox("UseDebugCamera(F1)", &useDebugCamera_);

	if (useDebugCamera_) {
		ImGui::Text("RightDrag:Look  WASD/QE:Move  ZC:Roll  Shift:Fast  R:Reset");
	} else {
		Transform& cameraTransform = defaultCamera_.GetTransform();
		ImGui::DragFloat3("CameraTranslate", &cameraTransform.translate.x, 0.01f);
		ImGui::DragFloat("CameraRotateX", &cameraTransform.rotate.x, 0.01f);
		ImGui::DragFloat("CameraRotateY", &cameraTransform.rotate.y, 0.01f);
		ImGui::DragFloat("CameraRotateZ", &cameraTransform.rotate.z, 0.01f);
	}

	ImGui::End();
#endif
}

const ICamera& FrameRenderer::GetActiveCamera() const {
	if (useDebugCamera_) {
		return debugCamera_;
	}

	return defaultCamera_;
}

} // namespace Homura
