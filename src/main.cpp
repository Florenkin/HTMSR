// 文件说明： 1
// 应用程序入口，负责组装项目服务、重建服务和主窗口。

#include <QApplication>

#include "app_shell/main_window.h"
#include "project_core/manifest_service.h"
#include "project_core/project_service.h"
#include "io_export/opencorr_poi_codec.h"
#include "io_export/result_exporter.h"
#include "reconstruction_core/epipolar_line_matcher.h"
#include "reconstruction_core/reconstruction_session_service.h"
#include "reconstruction_core/stereo_calibration_loader.h"
#include "reconstruction_core/stereo_line_laser_reconstruction_engine.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 组装项目层服务。

    htmsr::project_core::ProjectService projectService;
    htmsr::project_core::ManifestService manifestService;

    // 组装导出层服务。

    htmsr::io_export::OpenCorrPoiCodec poiCodec;
    htmsr::io_export::ResultExporter resultExporter(poiCodec);

    // 组装双目重建主链路服务。

    htmsr::reconstruction_core::StereoCalibrationLoader calibrationLoader;
    htmsr::reconstruction_core::EpipolarLineMatcher matcher;
    htmsr::reconstruction_core::StereoLineLaserReconstructionEngine reconstructionEngine;
    htmsr::reconstruction_core::ReconstructionSessionService sessionService(
        manifestService,
        calibrationLoader,
        matcher,
        reconstructionEngine,
        resultExporter);

    htmsr::app_shell::MainWindow window(
        projectService,
        manifestService,
        sessionService);

    // 启动主窗口并进入 Qt 事件循环。

    window.show();

    return app.exec();
}
