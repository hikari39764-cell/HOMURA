#include "Primitive.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Homura {

ModelData MakeSphereModel(const std::string& textureFilePath, uint32_t subdivision) {
	ModelData model;
	model.material.textureFilePath = textureFilePath;
	subdivision = std::clamp(subdivision, 3u, 128u);
	model.vertices.reserve(static_cast<size_t>(subdivision) * subdivision * 6);
	const float pi = std::numbers::pi_v<float>;
	auto makeVertex = [pi](float u, float v) {
		const float latitude = (v - 0.5f) * pi;
		const float longitude = u * pi * 2.0f;
		const Vector3 normal{ std::cos(latitude) * std::cos(longitude),
			std::sin(latitude), std::cos(latitude) * std::sin(longitude) };
		return VertexData{ { normal.x, normal.y, normal.z, 1.0f }, { u, 1.0f - v }, normal };
	};
	for (uint32_t latitude = 0; latitude < subdivision; ++latitude) {
		for (uint32_t longitude = 0; longitude < subdivision; ++longitude) {
			const float u = static_cast<float>(longitude) / subdivision;
			const float v = static_cast<float>(latitude) / subdivision;
			const float step = 1.0f / subdivision;
			const VertexData a = makeVertex(u, v);
			const VertexData b = makeVertex(u, v + step);
			const VertexData c = makeVertex(u + step, v);
			const VertexData d = makeVertex(u + step, v + step);
			// OBJと同じ面の向きで、緯度・経度の四角形を三角形に分ける
			model.vertices.insert(model.vertices.end(), { a, c, b, b, c, d });
		}
	}
	return model;
}

} // namespace Homura
