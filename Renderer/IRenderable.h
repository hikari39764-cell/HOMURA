#pragma once

#include <d3d12.h>

#include "Camera/ICamera.h"

namespace Homura {

/// <summary>
/// 描画可能なObjectの共通インターフェース
/// </summary>
class IRenderable {
public:
	virtual ~IRenderable() = default;

	virtual void UpdateTransformationMatrix(const ICamera& camera) = 0;
	virtual void Draw(ID3D12GraphicsCommandList* commandList) = 0;
};

} // namespace Homura
