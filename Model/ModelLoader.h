#pragma once

#include <string>

#include "ModelData.h"

struct aiNode;

namespace Homura {

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
// OBJとglTFを共通のモデルデータとして読み込む
ModelData LoadModelFile(const std::string& directoryPath, const std::string& filename);
// Assimpのノードを左手座標系の階層として読み込む
Node ReadNode(const aiNode* node);
// 既存のOBJ読み込み口も共通処理を使う
ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);

} // namespace Homura
