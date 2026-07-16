#pragma once

#include <string>
#include <vector>

#include "Math/MathUtil.h"

namespace Homura {

struct VertexData {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

struct MaterialData {
	std::string textureFilePath;
};

struct ModelData {
	std::vector<VertexData> vertices;
	MaterialData material;
};

} // namespace Homura
