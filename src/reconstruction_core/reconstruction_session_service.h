// 文件说明：
// 声明重建会话编排服务，串联校验、提线、匹配、重建和导出。

#pragma once

#include "data_model/reconstruction_types.h"
#include "io_export/result_exporter.h"
#include "project_core/manifest_service.h"
#include "project_core/project_document.h"
#include "reconstruction_core/icalibration_loader.h"
#include "reconstruction_core/imatcher.h"
#include "reconstruction_core/ireconstruction_engine.h"

namespace htmsr::reconstruction_core {

class ReconstructionSessionService {
public:
    ReconstructionSessionService(
        const project_core::ManifestService& manifestService,
        const ICalibrationLoader& calibrationLoader,
        const IMatcher& matcher,
        const IReconstructionEngine& reconstructionEngine,
        const io_export::ResultExporter& resultExporter);

    bool run(
        const project_core::ProjectDocument& document,
        data_model::ReconstructionSession& session,
        QStringList& logs) const;

private:
    const project_core::ManifestService& manifestService_;
    const ICalibrationLoader& calibrationLoader_;
    const IMatcher& matcher_;
    const IReconstructionEngine& reconstructionEngine_;
    const io_export::ResultExporter& resultExporter_;
};

}  // namespace htmsr::reconstruction_core
