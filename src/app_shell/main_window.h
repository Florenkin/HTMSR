// 文件说明：
// 声明主窗口，负责项目编排、参数输入和结果展示。

#pragma once

#include <optional>

#include <QMainWindow>

#include <opencv2/core.hpp>

#include "data_model/reconstruction_types.h"
#include "project_core/project_document.h"

namespace htmsr::app_shell {

class RibbonWidget;

}

namespace htmsr::visualization {
class ImageViewWidget;
class PointcloudSummaryWidget;
}

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QTableWidget;
class QTextEdit;
class QTreeWidget;

namespace htmsr::project_core {
class ManifestService;
class ProjectService;
}

namespace htmsr::reconstruction_core {
class ReconstructionSessionService;
}

namespace htmsr::app_shell {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(
        project_core::ProjectService& projectService,
        project_core::ManifestService& manifestService,
        reconstruction_core::ReconstructionSessionService& sessionService,
        QWidget* parent = nullptr);

private slots:
    void onNewProjectRequested();
    void onOpenProjectRequested();
    void onImportManifestRequested();
    void onImportCalibrationRequested();
    void onImportImagesRequested();
    void onRunReconstructionRequested();
    void onExportOutputsRequested();
    void onOpenSettingsRequested();

private:
    void setupUi();
    void appendLog(const QString& message);
    bool ensureProjectLoaded() const;
    void updateProjectTree();
    void updateSessionViews(const data_model::ReconstructionSession& session);
    void writeManifestTemplate(const QString& filePath) const;
    static QImage matToImage(const cv::Mat& image);

    project_core::ProjectService& projectService_;
    project_core::ManifestService& manifestService_;
    reconstruction_core::ReconstructionSessionService& sessionService_;

    std::optional<project_core::ProjectDocument> projectDocument_;
    std::optional<data_model::ReconstructionSession> lastSession_;

    RibbonWidget* ribbonWidget_ = nullptr;
    QTreeWidget* projectTree_ = nullptr;
    QListWidget* taskList_ = nullptr;
    visualization::ImageViewWidget* leftImageView_ = nullptr;
    visualization::ImageViewWidget* rightImageView_ = nullptr;
    visualization::ImageViewWidget* leftCenterlineView_ = nullptr;
    visualization::ImageViewWidget* rightCenterlineView_ = nullptr;
    visualization::ImageViewWidget* matchView_ = nullptr;
    visualization::PointcloudSummaryWidget* pointcloudView_ = nullptr;
    QTableWidget* resultTable_ = nullptr;
    QTextEdit* logView_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QComboBox* centerlineMethodCombo_ = nullptr;
    QDoubleSpinBox* bwThresholdSpin_ = nullptr;
    QDoubleSpinBox* sumThresholdSpin_ = nullptr;
    QDoubleSpinBox* matchingDistanceSpin_ = nullptr;
};

}  // namespace htmsr::app_shell
