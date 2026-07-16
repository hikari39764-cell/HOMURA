#pragma once

#include "Math/MathUtil.h"

namespace Homura {

/// <summary>
/// 描画用カメラの共通インターフェース
/// </summary>
class ICamera {
public:
	virtual ~ICamera() = default;

	virtual const Matrix4x4& GetViewMatrix() const = 0;
	virtual const Matrix4x4& GetProjectionMatrix() const = 0;
};

} // namespace Homura
