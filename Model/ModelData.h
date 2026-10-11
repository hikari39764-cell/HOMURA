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

struct Node {
	Matrix4x4 localMatrix = MakeIdentity4x4();
	std::string name;
	std::vector<Node> children;
};

struct ModelData {
	std::vector<VertexData> vertices;
	MaterialData material;
	Node rootNode;
};

} // namespace Homura
