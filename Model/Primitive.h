#pragma once

#include <cstdint>

#include "Model/ModelData.h"

namespace Homura {

// 光の当たり方を確認するための球を作成する
ModelData MakeSphereModel(const std::string& textureFilePath, uint32_t subdivision = 32);

} // namespace Homura
