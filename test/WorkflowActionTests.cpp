#include "app/ui/AcquisitionPanel.h"

#include <QAction>
#include <QLineEdit>
#include <QPushButton>
#include <stdexcept>

using htmsr::app::AcquisitionPanel;

namespace {
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

void runWorkflowActionTests()
{
    AcquisitionPanel panel;
    using Action = AcquisitionPanel::WorkflowAction;
    auto* capture = panel.workflowAction(Action::CaptureCalibration);
    auto* calibrate = panel.workflowAction(Action::Calibrate);
    auto* scan = panel.workflowAction(Action::CaptureReconstruction);
    auto* refresh = panel.workflowAction(Action::RefreshDevices);
    auto* reconstruction = panel.workflowAction(Action::Reconstruct);
    auto* calibrationExport = panel.workflowAction(Action::ExportCalibration);
    auto* cloudExport = panel.workflowAction(Action::ExportReconstruction);
    require(capture && calibrate && scan && refresh && reconstruction && calibrationExport && cloudExport,
        "All ribbon workflow actions must be available");
    require(!calibrate->isEnabled() && !reconstruction->isEnabled(),
        "Algorithms must be unavailable without paired input directories");
    require(!calibrationExport->isEnabled() && !cloudExport->isEnabled(),
        "Exports must be unavailable without results");

    int captures = 0;
    QObject::connect(&panel, &AcquisitionPanel::captureCalibrationFrameRequested, [&captures]() { ++captures; });
    capture->trigger();
    require(captures == 1, "Ribbon capture must reach the existing acquisition workflow once");
    auto* captureButton = panel.findChild<QPushButton*>("captureCalibrationFrame");
    require(captureButton, "Existing capture button must remain available");
    captureButton->click();
    require(captures == 2, "Panel capture must share the same workflow without double dispatch");

    panel.findChild<QLineEdit*>("leftCalibrationDirectory")->setText("left");
    panel.findChild<QLineEdit*>("rightCalibrationDirectory")->setText("right");
    panel.findChild<QLineEdit*>("leftReconstructionDirectory")->setText("left");
    panel.findChild<QLineEdit*>("rightReconstructionDirectory")->setText("right");
    require(calibrate->isEnabled() && reconstruction->isEnabled(),
        "Ribbon algorithm actions must follow edited input paths");
    panel.setResultAvailability(true, true);
    require(calibrationExport->isEnabled() && cloudExport->isEnabled(),
        "Ribbon exports must become available with valid results");

    panel.setCalibrationCaptureState(true, 1);
    require(!refresh->isEnabled() && !scan->isEnabled() && capture->isEnabled(),
        "An active calibration capture session must block device refresh and scanning");
    panel.setBusy(true);
    for (int index = 0; index < static_cast<int>(Action::Count); ++index)
        require(!panel.workflowAction(static_cast<Action>(index))->isEnabled(),
            "Busy tasks must disable all ribbon workflow actions");
    capture->trigger();
    captureButton->click();
    require(captures == 2, "Disabled ribbon and panel actions must not dispatch a capture");

    int finishes = 0;
    QObject::connect(&panel, &AcquisitionPanel::finishCalibrationCaptureRequested, [&finishes]() { ++finishes; });
    panel.showWorkflowPage(1);
    require(finishes == 0, "Busy tasks must prevent workflow switches from finishing a capture session");
    panel.setBusy(false);
    panel.showWorkflowPage(1);
    require(finishes == 1, "Switching from calibration to reconstruction must finish the capture session");
    panel.setCalibrationCaptureState(false, 1);
    require(refresh->isEnabled() && scan->isEnabled(), "Idle device actions must recover after capture finishes");
    panel.setResultAvailability(false, false);
    require(!calibrationExport->isEnabled() && !cloudExport->isEnabled(),
        "Invalidated results must disable ribbon exports again");
}
