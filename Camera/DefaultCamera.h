#pragma once

#include "Camera/ICamera.h"

namespace Homura {

/// <summary>
/// 通常描画用のカメラ
/// </summary>
class DefaultCamera : public ICamera {
public:
	void Initialize(float aspectRatio);
	void UpdateMatrix();

	Transform& GetTransform();
	const Transform& GetTransform() const;

	const Matrix4x4& GetViewMatrix() const override;
	const Matrix4x4& GetProjectionMatrix() const override;

private:
	// カメラの座標変換行列の初期値
	Transform transform_ = {
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, -10.0f },
	};

	Matrix4x4 viewMatrix_ = MakeIdentity4x4();
	Matrix4x4 projectionMatrix_ = MakeIdentity4x4();
};

} // namespace Homura
