#pragma once

#include "app/services/AppConfigService.h"
#include "app/services/QtLogSink.h"
#include "core/CalibrationService.h"
#include "core/PointCloudService.h"
#include "core/ReconstructionService.h"

#include <QFutureWatcher>
#include <QMainWindow>

class QProgressBar;
class QTreeWidget;

namespace htmsr::app {

class ImageViewWidget;
class LogPanel;
class ParameterPanel;
class PointCloudViewWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void runCalibration();
    void loadCalibration();
    void runReconstruction();
    void exportTxt();
    void exportPcd();
    void saveProjectSettings();
    void onCalibrationFinished();
    void onReconstructionFinished();

private:
    void buildMenus();
    void buildToolBar();
    void buildDocks();
    void buildCentralView();
    void refreshProjectTree();
    void setBusy(bool busy, const QString& text);
    QString outputPath(const QString& filename) const;

    AppConfigService configService_;
    CalibrationService calibrationService_;
    ReconstructionService reconstructionService_;
    PointCloudService pointCloudService_;
    QtLogSink* logSink_ = nullptr;

    ParameterPanel* parameterPanel_ = nullptr;
    LogPanel* logPanel_ = nullptr;
    PointCloudViewWidget* pointCloudView_ = nullptr;
    ImageViewWidget* leftImageView_ = nullptr;
    ImageViewWidget* rightImageView_ = nullptr;
    ImageViewWidget* debugImageView_ = nullptr;
    QTreeWidget* projectTree_ = nullptr;
    QProgressBar* progressBar_ = nullptr;

    CalibrationResult calibration_;
    ReconstructionResult reconstruction_;
    QFutureWatcher<CalibrationResult> calibrationWatcher_;
    QFutureWatcher<ReconstructionResult> reconstructionWatcher_;
};

} // namespace htmsr::app
