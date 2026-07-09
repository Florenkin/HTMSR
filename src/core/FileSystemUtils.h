#pragma once

#include "core/Types.h"

#include <string>
#include <vector>

namespace htmsr {

std::vector<std::string> listImageFiles(const std::string& directory, const ImageRange& range = {});
bool ensureParentDirectory(const std::string& filePath);

} // namespace htmsr
