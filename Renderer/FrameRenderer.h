#pragma once

#include <array>
#include <chrono>

#include "Camera/DefaultCamera.h"
#include "DebugTools/DebugCamera.h"
#include "DebugTools/Gui/DebugGui.h"
#include "Graphics/D3D12Context.h"
#include "Renderer/Object3dRenderer.h"
#include "Renderer/ParticleRenderer.h"

namespace Homura {

class Input;
class WinApp;

/// <summary>
/// 1フレームの描画と、各章の表示切り替えを管理する
/// </summary>
class FrameRenderer {
public:
	enum class DemoScene { Blend, AlphaTest, Instancing, Particle, Specular, Point, Spot, Model, Count };
	bool Initialize(WinApp* winApp);
	void Finalize();
	void Update(const Input& input);
	bool Draw();
	void SetDemoScene(DemoScene scene);
	DemoScene GetDemoScene() const;

private:
	bool CreateRenderers();
	bool CreateDebugGui(HWND hwnd);
	void DrawDebugGui();
	void DrawObjects();
	const ICamera& GetActiveCamera() const;

	// ImGui、インスタンス、Textureが同じSRVを上書きしないように分ける
	static constexpr UINT kImGuiSRVIndex = 0;
	static constexpr UINT kModelTextureSRVIndex = 1;
	static constexpr UINT kParticleInstanceSRVIndex = 2;
	static constexpr UINT kParticleTextureSRVIndex = 3;
	static constexpr UINT kObjectTextureSRVIndex = 4;
	static constexpr size_t kObjectCount = 6;
	static constexpr size_t kBackground = 0;
	static constexpr size_t kBlendPlane = 1;
	static constexpr size_t kFence = 2;
	static constexpr size_t kSphere = 3;
	static constexpr size_t kObj = 4;
	static constexpr size_t kGltf = 5;

	D3D12Context d3d12Context_;
	DebugGui debugGui_;
	std::array<Object3dRenderer, kObjectCount> objects_;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kObjectCount> intermediateResources_;
	ParticleRenderer particles_;
	TextureManager particleTexture_;
	TextureManager checkerTexture_;
	Microsoft::WRL::ComPtr<ID3D12Resource> particleIntermediate_;
	Microsoft::WRL::ComPtr<ID3D12Resource> checkerIntermediate_;
	DefaultCamera defaultCamera_;
	DebugCamera debugCamera_;
	DemoScene demoScene_ = DemoScene::Blend;
	bool useDebugCamera_ = false;
	bool paused_ = false;
	bool initialized_ = false;
	float deltaTime_ = 1.0f / 60.0f;
	std::chrono::steady_clock::time_point previousFrame_{};
};

} // namespace Homura
