#pragma once

#include "MathUtil.h"

class Input;

/// <summary>
/// デバッグカメラ
/// </summary>
class DebugCamera {
public:
	void Initialize(float aspectRatio);
	void Update(const Input& input);
	void Reset();

	const Matrix4x4& GetViewMatrix() const;
	const Matrix4x4& GetProjectionMatrix() const;

private:
	void UpdateMove(const Input& input);
	void UpdateRotate(const Input& input);
	void UpdateMatrix();
	Vector3 TransformDirection(const Vector3& direction) const;

private:
	// ローカル座標
	Vector3 translation_ = { 0.0f, 0.0f, -50.0f };

	// 累積回転行列
	Matrix4x4 matRot_ = MakeIdentity4x4();

	// ビュー行列
	Matrix4x4 viewMatrix_ = MakeIdentity4x4();

	// 射影行列
	Matrix4x4 projectionMatrix_ = MakeIdentity4x4();
};
