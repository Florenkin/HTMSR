// 文件说明：
// 声明顶部 Ribbon 操作区组件。

#pragma once

#include <QToolButton>
#include <QWidget>

namespace htmsr::app_shell {

class RibbonWidget : public QWidget {
    Q_OBJECT

public:
    explicit RibbonWidget(QWidget* parent = nullptr);

signals:
    void newProjectRequested();
    void openProjectRequested();
    void importManifestRequested();
    void importCalibrationRequested();
    void importImagesRequested();
    void runReconstructionRequested();
    void retryFailedRequested();
    void exportOutputsRequested();
    void openSettingsRequested();

private:
    QToolButton* createButton(const QString& text, const QString& description);
};

}  // namespace htmsr::app_shell
