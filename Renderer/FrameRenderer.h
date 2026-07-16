#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>

#include "Camera/DefaultCamera.h"
#include "DebugTools/DebugCamera.h"
#include "DebugTools/Gui/DebugGui.h"
#include "Graphics/D3D12Context.h"
#include "Model/ModelData.h"
#include "Renderer/Object3dRenderer.h"

namespace Homura {

class Input;

/// <summary>
/// 1フレームの描画全体を管理する
/// </summary>
class FrameRenderer {
public:
	bool Initialize(HWND hwnd);
	void Finalize();

	void Update(const Input& input);
	void Draw();

private:
	bool CreateObject3dRenderer();
	bool CreateDebugGui(HWND hwnd);
	void DrawDebugGui();
	const ICamera& GetActiveCamera() const;

private:
	static constexpr UINT kImGuiSRVIndex = 0;
	static constexpr UINT kModelTextureSRVIndex = 1;
	static constexpr int kBackBufferCount = 2;
	static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

private:
	D3D12Context d3d12Context_;
	DebugGui debugGui_;
	Object3dRenderer object3dRenderer_;

	ModelData modelData_;
	Microsoft::WRL::ComPtr<ID3D12Resource> modelIntermediateResource_;

	DefaultCamera defaultCamera_;
	DebugCamera debugCamera_;
	bool useDebugCamera_ = false;
};

} // namespace Homura
