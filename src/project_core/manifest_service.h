// 文件说明：
// 声明双目 manifest 的校验和模板生成服务。

#pragma once

#include "data_model/reconstruction_types.h"
#include "project_core/project_document.h"

namespace htmsr::project_core {

class ManifestService {
public:
    [[nodiscard]] data_model::ManifestValidationResult loadAndValidate(const ProjectDocument& document) const;
    [[nodiscard]] QString manifestTemplate() const;
};

}  // namespace htmsr::project_core
