#pragma once

#include <string>

#include "ModelData.h"

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);
