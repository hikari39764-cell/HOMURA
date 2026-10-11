#include "ModelLoader.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "DebugTools/Logger/Logger.h"

namespace Homura {

namespace {

	std::string JoinPath(const std::string& directoryPath, const std::string& filename) {
		std::string relativePath = filename;
		std::replace(relativePath.begin(), relativePath.end(), '\\', '/');
		return (std::filesystem::path(directoryPath) / relativePath).lexically_normal().generic_string();
	}

	Matrix4x4 ConvertNodeMatrix(const aiMatrix4x4& matrix) {
		Matrix4x4 result{};
		constexpr float axisSigns[] = { -1.0f, 1.0f, 1.0f, 1.0f };

		// X軸の向きを合わせてから、列ベクトル用の行列を転置する
		for (unsigned int row = 0; row < 4; ++row) {
			for (unsigned int column = 0; column < 4; ++column) {
				result.m[row][column] = matrix[column][row] * axisSigns[row] * axisSigns[column];
			}
		}
		return result;
	}

	bool ReadMaterial(const aiScene* scene, const aiMesh* mesh, const std::string& directoryPath,
		MaterialData& material) {
		if (mesh->mMaterialIndex >= scene->mNumMaterials) {
			Log("Failed to load model material: material index is out of range.\n");
			return false;
		}

		const aiMaterial* source = scene->mMaterials[mesh->mMaterialIndex];
		aiString texturePath;
		// glTFのBaseColorとOBJのDiffuseを同じTextureとして扱う
		if (source->GetTexture(aiTextureType_BASE_COLOR, 0, &texturePath) != AI_SUCCESS &&
			source->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) != AI_SUCCESS) {
			Log("Failed to load model material: an external color texture is required.\n");
			return false;
		}
		if (scene->GetEmbeddedTexture(texturePath.C_Str()) != nullptr) {
			Log("Failed to load model material: save the color texture as an external image.\n");
			return false;
		}

		material.textureFilePath = JoinPath(directoryPath, texturePath.C_Str());
		std::error_code error;
		if (!std::filesystem::is_regular_file(material.textureFilePath, error)) {
			Log(std::format("Failed to find model texture: {}\n", material.textureFilePath));
			return false;
		}
		return true;
	}

	bool AppendNodeMeshes(const aiScene* scene, const aiNode* node, const aiMatrix4x4& parentMatrix,
		float rootDeterminant, bool isRoot, const std::string& directoryPath, ModelData& modelData) {
		// 根ノードは描画時に適用するので、ここでは子ノードだけを頂点へ反映する
		const aiMatrix4x4 nodeMatrix = isRoot ? aiMatrix4x4() : parentMatrix * node->mTransformation;
		aiMatrix3x3 normalMatrix(nodeMatrix);
		const float determinant = normalMatrix.Determinant();
		if (std::abs(determinant) < 1.0e-8f) {
			Log("Failed to load model node: the scale is zero.\n");
			return false;
		}
		normalMatrix.Inverse().Transpose();
		const bool reverseWinding = determinant * rootDeterminant < 0.0f;

		for (unsigned int meshIndex = 0; meshIndex < node->mNumMeshes; ++meshIndex) {
			const aiMesh* mesh = scene->mMeshes[node->mMeshes[meshIndex]];
			if (!mesh->HasNormals() || !mesh->HasTextureCoords(0)) {
				Log("Failed to load model mesh: normals and texture coordinates are required.\n");
				return false;
			}

			MaterialData material;
			if (!ReadMaterial(scene, mesh, directoryPath, material)) {
				return false;
			}
			if (modelData.material.textureFilePath.empty()) {
				modelData.material = material;
			} else if (modelData.material.textureFilePath != material.textureFilePath) {
				// 現在の描画はTextureを1枚だけ使うので、別Textureを混ぜない
				Log("Failed to load model mesh: multiple color textures are not supported.\n");
				return false;
			}

			for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
				const aiFace& face = mesh->mFaces[faceIndex];
				if (face.mNumIndices != 3) {
					Log("Failed to load model mesh: only triangles can be drawn.\n");
					return false;
				}

				for (unsigned int corner = 0; corner < 3; ++corner) {
					// 負のScaleを持つノードでも、表面の向きを保つ
					const unsigned int vertexIndex = face.mIndices[reverseWinding ? 2 - corner : corner];
					const aiVector3D position = nodeMatrix * mesh->mVertices[vertexIndex];
					aiVector3D normal = normalMatrix * mesh->mNormals[vertexIndex];
					normal.NormalizeSafe();
					const aiVector3D& texcoord = mesh->mTextureCoords[0][vertexIndex];

					VertexData vertex{};
					// 座標と法線のX反転はここで一度だけ行う
					vertex.position = { -position.x, position.y, position.z, 1.0f };
					vertex.normal = { -normal.x, normal.y, normal.z };
					vertex.texcoord = { texcoord.x, texcoord.y };
					modelData.vertices.push_back(vertex);
				}
			}
		}

		for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
			if (!AppendNodeMeshes(scene, node->mChildren[childIndex], nodeMatrix, rootDeterminant,
				false, directoryPath, modelData)) {
				return false;
			}
		}
		return true;
	}

} // namespace

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
	MaterialData materialData{};
	std::ifstream file(JoinPath(directoryPath, filename));
	std::string line;
	while (std::getline(file, line)) {
		std::istringstream lineStream(line);
		std::string identifier;
		lineStream >> identifier;
		if (identifier == "map_Kd") {
			std::string textureFilename;
			lineStream >> textureFilename;
			materialData.textureFilePath = JoinPath(directoryPath, textureFilename);
		}
	}
	return materialData;
}

Node ReadNode(const aiNode* node) {
	Node result;
	if (node == nullptr) {
		return result;
	}
	result.name = node->mName.C_Str();
	result.localMatrix = ConvertNodeMatrix(node->mTransformation);
	result.children.reserve(node->mNumChildren);
	for (unsigned int index = 0; index < node->mNumChildren; ++index) {
		result.children.push_back(ReadNode(node->mChildren[index]));
	}
	return result;
}

ModelData LoadModelFile(const std::string& directoryPath, const std::string& filename) {
	Assimp::Importer importer;
	const std::string filePath = JoinPath(directoryPath, filename);
	// UVと面の向きはAssimpで変換し、X軸の変換は頂点を取り出す時に行う
	const aiScene* scene = importer.ReadFile(filePath,
		aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs | aiProcess_FlipWindingOrder);
	if (scene == nullptr || scene->mRootNode == nullptr || !scene->HasMeshes()) {
		Log(std::format("Failed to load model: {} ({})\n", filePath, importer.GetErrorString()));
		return {};
	}

	const float rootDeterminant = aiMatrix3x3(scene->mRootNode->mTransformation).Determinant();
	if (std::abs(rootDeterminant) < 1.0e-8f) {
		Log("Failed to load model root: the scale is zero.\n");
		return {};
	}

	ModelData modelData;
	modelData.rootNode = ReadNode(scene->mRootNode);
	const std::string modelDirectory = std::filesystem::path(filePath).parent_path().generic_string();
	if (!AppendNodeMeshes(scene, scene->mRootNode, aiMatrix4x4(), rootDeterminant,
		true, modelDirectory, modelData)) {
		return {};
	}
	return modelData;
}

ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename) {
	return LoadModelFile(directoryPath, filename);
}

} // namespace Homura
