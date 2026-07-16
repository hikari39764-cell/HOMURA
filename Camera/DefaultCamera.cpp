#include "DefaultCamera.h"

namespace Homura {

namespace {

constexpr float kFovY = 0.45f;
constexpr float kNearClip = 0.1f;
constexpr float kFarClip = 100.0f;

} // namespace

void DefaultCamera::Initialize(float aspectRatio) {
	projectionMatrix_ = MakePerspectiveFovMatrix(kFovY, aspectRatio, kNearClip, kFarClip);
	UpdateMatrix();
}

void DefaultCamera::UpdateMatrix() {
	// 通常カメラのViewMatrixを作成する
	Matrix4x4 cameraMatrix = MakeAffineMatrix(
		transform_.scale,
		transform_.rotate,
		transform_.translate
	);

	viewMatrix_ = Inverse(cameraMatrix);
}

Transform& DefaultCamera::GetTransform() {
	return transform_;
}

const Transform& DefaultCamera::GetTransform() const {
	return transform_;
}

const Matrix4x4& DefaultCamera::GetViewMatrix() const {
	return viewMatrix_;
}

const Matrix4x4& DefaultCamera::GetProjectionMatrix() const {
	return projectionMatrix_;
}

} // namespace Homura
