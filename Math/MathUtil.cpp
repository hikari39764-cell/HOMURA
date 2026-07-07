#include "MathUtil.h"

#include <cassert>
#include <cmath>

Matrix4x4 MakeIdentity4x4() {
	Matrix4x4 result{};

	result.m[0][0] = 1.0f;
	result.m[1][1] = 1.0f;
	result.m[2][2] = 1.0f;
	result.m[3][3] = 1.0f;

	return result;
}

Matrix4x4 Multiply(const Matrix4x4& matrix1, const Matrix4x4& matrix2) {
	Matrix4x4 result{};

	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			for (int index = 0; index < 4; ++index) {
				result.m[row][column] += matrix1.m[row][index] * matrix2.m[index][column];
			}
		}
	}

	return result;
}

Matrix4x4 MakeScaleMatrix(const Vector3& scale) {
	Matrix4x4 result{};

	result.m[0][0] = scale.x;
	result.m[1][1] = scale.y;
	result.m[2][2] = scale.z;
	result.m[3][3] = 1.0f;

	return result;
}

Matrix4x4 MakeRotateXMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();

	float sin = std::sin(radian);
	float cos = std::cos(radian);

	result.m[1][1] = cos;
	result.m[1][2] = sin;
	result.m[2][1] = -sin;
	result.m[2][2] = cos;

	return result;
}

Matrix4x4 MakeRotateYMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();

	float sin = std::sin(radian);
	float cos = std::cos(radian);

	result.m[0][0] = cos;
	result.m[0][2] = -sin;
	result.m[2][0] = sin;
	result.m[2][2] = cos;

	return result;
}

Matrix4x4 MakeRotateZMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();

	float sin = std::sin(radian);
	float cos = std::cos(radian);

	result.m[0][0] = cos;
	result.m[0][1] = sin;
	result.m[1][0] = -sin;
	result.m[1][1] = cos;

	return result;
}

Matrix4x4 MakeTranslateMatrix(const Vector3& translate) {
	Matrix4x4 result = MakeIdentity4x4();

	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;

	return result;
}

Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

	Matrix4x4 rotateMatrix = Multiply(rotateXMatrix, Multiply(rotateYMatrix, rotateZMatrix));
	Matrix4x4 worldMatrix = Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);

	return worldMatrix;
}

Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {
	Matrix4x4 result{};

	float cot = 1.0f / std::tan(fovY / 2.0f);

	result.m[0][0] = cot / aspectRatio;
	result.m[1][1] = cot;
	result.m[2][2] = farClip / (farClip - nearClip);
	result.m[2][3] = 1.0f;
	result.m[3][2] = (-nearClip * farClip) / (farClip - nearClip);

	return result;
}

Matrix4x4 MakeOrthographicMatrix(
	float left,
	float top,
	float right,
	float bottom,
	float nearClip,
	float farClip
) {
	Matrix4x4 result{};

	result.m[0][0] = 2.0f / (right - left);
	result.m[1][1] = 2.0f / (top - bottom);
	result.m[2][2] = 1.0f / (farClip - nearClip);
	result.m[3][0] = (left + right) / (left - right);
	result.m[3][1] = (top + bottom) / (bottom - top);
	result.m[3][2] = nearClip / (nearClip - farClip);
	result.m[3][3] = 1.0f;

	return result;
}

Matrix4x4 Inverse(const Matrix4x4& matrix) {
	Matrix4x4 result = MakeIdentity4x4();

	float work[4][8] = {};

	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			work[row][column] = matrix.m[row][column];
		}

		work[row][row + 4] = 1.0f;
	}

	for (int column = 0; column < 4; ++column) {
		int pivot = column;
		float max = std::fabs(work[column][column]);

		for (int row = column + 1; row < 4; ++row) {
			float value = std::fabs(work[row][column]);
			if (value > max) {
				max = value;
				pivot = row;
			}
		}

		assert(max != 0.0f);

		if (max == 0.0f) {
			return MakeIdentity4x4();
		}

		if (pivot != column) {
			for (int index = 0; index < 8; ++index) {
				float temporary = work[column][index];
				work[column][index] = work[pivot][index];
				work[pivot][index] = temporary;
			}
		}

		float pivotValue = work[column][column];

		for (int index = 0; index < 8; ++index) {
			work[column][index] /= pivotValue;
		}

		for (int row = 0; row < 4; ++row) {
			if (row == column) {
				continue;
			}

			float factor = work[row][column];

			for (int index = 0; index < 8; ++index) {
				work[row][index] -= factor * work[column][index];
			}
		}
	}

	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			result.m[row][column] = work[row][column + 4];
		}
	}

	return result;
}

float Length(const Vector3& vector) {
	return std::sqrt(
		vector.x * vector.x +
		vector.y * vector.y +
		vector.z * vector.z
	);
}

Vector3 Normalize(const Vector3& vector) {
	float length = Length(vector);

	if (length == 0.0f) {
		return { 0.0f, 0.0f, 0.0f };
	}

	return {
		vector.x / length,
		vector.y / length,
		vector.z / length
	};
}