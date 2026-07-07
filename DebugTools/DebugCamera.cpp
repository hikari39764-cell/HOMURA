#include "DebugCamera.h"

#include "Input/Input.h"

namespace {

constexpr float kMoveSpeed = 0.25f;
constexpr float kFastMoveSpeed = 1.0f;
constexpr float kMouseRotateSpeed = 0.005f;
constexpr float kRollRotateSpeed = 0.03f;
constexpr float kFovY = 0.45f;
constexpr float kNearClip = 0.1f;
constexpr float kFarClip = 100.0f;

bool HasValue(const Vector3& vector) {
	return vector.x != 0.0f || vector.y != 0.0f || vector.z != 0.0f;
}

Vector3 Add(const Vector3& lhs, const Vector3& rhs) {
	return {
		lhs.x + rhs.x,
		lhs.y + rhs.y,
		lhs.z + rhs.z
	};
}

} // namespace

void DebugCamera::Initialize(float aspectRatio) {
	projectionMatrix_ = MakePerspectiveFovMatrix(kFovY, aspectRatio, kNearClip, kFarClip);
	Reset();
}

void DebugCamera::Update(const Input& input) {
	if (input.IsTriggerKey(DIK_R)) {
		Reset();
		return;
	}

	UpdateRotate(input);
	UpdateMove(input);
	UpdateMatrix();
}

void DebugCamera::Reset() {
	translation_ = { 0.0f, 0.0f, -50.0f };
	matRot_ = MakeIdentity4x4();
	UpdateMatrix();
}

const Matrix4x4& DebugCamera::GetViewMatrix() const {
	return viewMatrix_;
}

const Matrix4x4& DebugCamera::GetProjectionMatrix() const {
	return projectionMatrix_;
}

void DebugCamera::UpdateMove(const Input& input) {
	const float speed =
		(input.IsPressKey(DIK_LSHIFT) || input.IsPressKey(DIK_RSHIFT)) ? kFastMoveSpeed : kMoveSpeed;

	Vector3 move = { 0.0f, 0.0f, 0.0f };

	if (input.IsPressKey(DIK_W)) {
		move.z += speed;
	}

	if (input.IsPressKey(DIK_S)) {
		move.z -= speed;
	}

	if (input.IsPressKey(DIK_D)) {
		move.x += speed;
	}

	if (input.IsPressKey(DIK_A)) {
		move.x -= speed;
	}

	if (input.IsPressKey(DIK_E)) {
		move.y += speed;
	}

	if (input.IsPressKey(DIK_Q)) {
		move.y -= speed;
	}

	if (!HasValue(move)) {
		return;
	}

	// カメラの向きに合わせてローカル移動量を回転させる
	translation_ = Add(translation_, TransformDirection(move));
}

void DebugCamera::UpdateRotate(const Input& input) {
	Vector3 rotate = { 0.0f, 0.0f, 0.0f };

	// 右ドラッグの移動量を追加回転として使う
	if (input.IsPressMouse(1)) {
		MouseMove mouseMove = input.GetMouseMove();
		rotate.x += static_cast<float>(mouseMove.y) * kMouseRotateSpeed;
		rotate.y += static_cast<float>(mouseMove.x) * kMouseRotateSpeed;
	}

	if (input.IsPressKey(DIK_C)) {
		rotate.z += kRollRotateSpeed;
	}

	if (input.IsPressKey(DIK_Z)) {
		rotate.z -= kRollRotateSpeed;
	}

	if (!HasValue(rotate)) {
		return;
	}

	// 追加回転分を累積回転行列に合成する
	Matrix4x4 matRotDelta = MakeIdentity4x4();
	matRotDelta = Multiply(matRotDelta, MakeRotateXMatrix(rotate.x));
	matRotDelta = Multiply(matRotDelta, MakeRotateYMatrix(rotate.y));
	matRotDelta = Multiply(matRotDelta, MakeRotateZMatrix(rotate.z));
	matRot_ = Multiply(matRotDelta, matRot_);
}

void DebugCamera::UpdateMatrix() {
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translation_);
	Matrix4x4 worldMatrix = Multiply(matRot_, translateMatrix);

	// カメラのWorldMatrixの逆行列をビュー行列にする
	viewMatrix_ = Inverse(worldMatrix);
}

Vector3 DebugCamera::TransformDirection(const Vector3& direction) const {
	return {
		direction.x * matRot_.m[0][0] + direction.y * matRot_.m[1][0] + direction.z * matRot_.m[2][0],
		direction.x * matRot_.m[0][1] + direction.y * matRot_.m[1][1] + direction.z * matRot_.m[2][1],
		direction.x * matRot_.m[0][2] + direction.y * matRot_.m[1][2] + direction.z * matRot_.m[2][2]
	};
}
