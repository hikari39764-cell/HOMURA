#include "ModelLoader.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

	std::string JoinPath(const std::string& directoryPath, const std::string& filename) {
		if (directoryPath.empty()) {
			return filename;
		}

		const char last = directoryPath.back();
		if (last == '/' || last == '\\') {
			return directoryPath + filename;
		}

		return directoryPath + "/" + filename;
	}

	struct VertexIndexData {
		uint32_t positionIndex = 0;
		uint32_t texcoordIndex = 0;
		uint32_t normalIndex = 0;
	};

	VertexIndexData ParseFaceVertex(const std::string& faceVertex) {
		VertexIndexData indexData{};
		std::istringstream faceVertexStream(faceVertex);
		std::string indexString;

		// position / texcoord / normal の順でIndexを読む
		std::getline(faceVertexStream, indexString, '/');
		assert(!indexString.empty());
		indexData.positionIndex = static_cast<uint32_t>(std::stoul(indexString));

		std::getline(faceVertexStream, indexString, '/');
		assert(!indexString.empty());
		indexData.texcoordIndex = static_cast<uint32_t>(std::stoul(indexString));

		std::getline(faceVertexStream, indexString, '/');
		assert(!indexString.empty());
		indexData.normalIndex = static_cast<uint32_t>(std::stoul(indexString));

		return indexData;
	}

	VertexData MakeVertexData(
		const VertexIndexData& indexData,
		const std::vector<Vector4>& positions,
		const std::vector<Vector2>& texcoords,
		const std::vector<Vector3>& normals
	) {
		assert(indexData.positionIndex > 0 && indexData.positionIndex <= positions.size());
		assert(indexData.texcoordIndex > 0 && indexData.texcoordIndex <= texcoords.size());
		assert(indexData.normalIndex > 0 && indexData.normalIndex <= normals.size());

		VertexData vertex{};
		vertex.position = positions[indexData.positionIndex - 1];
		vertex.texcoord = texcoords[indexData.texcoordIndex - 1];
		vertex.normal = normals[indexData.normalIndex - 1];

		// OBJの右手座標系から、DirectXで扱う左手座標系に変換する
		vertex.position.x *= -1.0f;
		vertex.normal.x *= -1.0f;

		return vertex;
	}

} // namespace

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
	MaterialData materialData{};

	// material template libraryを開く
	const std::string filePath = JoinPath(directoryPath, filename);
	std::ifstream file(filePath);
	assert(file.is_open());

	if (!file.is_open()) {
		return materialData;
	}

	std::string line;
	while (std::getline(file, line)) {
		std::istringstream lineStream(line);
		std::string identifier;
		lineStream >> identifier;

		if (identifier == "map_Kd") {
			std::string textureFilename;
			lineStream >> textureFilename;

			// TextureはMaterialと同じフォルダから探す
			materialData.textureFilePath = JoinPath(directoryPath, textureFilename);
		}
	}

	return materialData;
}

ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename) {
	ModelData modelData{};
	std::vector<Vector4> positions;
	std::vector<Vector2> texcoords;
	std::vector<Vector3> normals;

	// OBJファイルを開く
	const std::string filePath = JoinPath(directoryPath, filename);
	std::ifstream file(filePath);
	assert(file.is_open());

	if (!file.is_open()) {
		return modelData;
	}

	std::string line;
	while (std::getline(file, line)) {
		std::istringstream lineStream(line);
		std::string identifier;
		lineStream >> identifier;

		if (identifier == "v") {
			Vector4 position{};
			lineStream >> position.x >> position.y >> position.z;
			position.w = 1.0f;
			positions.push_back(position);
		} else if (identifier == "vt") {
			Vector2 texcoord{};
			lineStream >> texcoord.x >> texcoord.y;

			// OBJは左下原点なので、DirectXで扱う左上原点へ合わせる
			texcoord.y = 1.0f - texcoord.y;
			texcoords.push_back(texcoord);
		} else if (identifier == "vn") {
			Vector3 normal{};
			lineStream >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);
		} else if (identifier == "f") {
			std::vector<VertexData> faceVertices;
			std::string faceVertex;

			// Faceの各頂点を position / texcoord / normal として読む
			while (lineStream >> faceVertex) {
				const VertexIndexData indexData = ParseFaceVertex(faceVertex);
				faceVertices.push_back(MakeVertexData(indexData, positions, texcoords, normals));
			}

			if (faceVertices.size() < 3) {
				continue;
			}

			// 左手座標系にしたので、三角形の表裏が合うように逆順で積む
			for (size_t index = 1; index + 1 < faceVertices.size(); ++index) {
				modelData.vertices.push_back(faceVertices[index + 1]);
				modelData.vertices.push_back(faceVertices[index]);
				modelData.vertices.push_back(faceVertices[0]);
			}
		} else if (identifier == "mtllib") {
			std::string materialFilename;
			lineStream >> materialFilename;
			modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
		}
	}

	return modelData;
}
