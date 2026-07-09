#pragma once

#include "core/Types.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QSpinBox;

namespace htmsr::app {

class ParameterPanel final : public QWidget {
    Q_OBJECT

public:
    explicit ParameterPanel(QWidget* parent = nullptr);

    void setProjectConfig(const AppProjectConfig& config);
    AppProjectConfig projectConfig() const;
    CalibrationInput calibrationInput() const;
    ReconstructionInput reconstructionInput(const CalibrationResult& calibration) const;

private:
    QLineEdit* createPathRow(QFormLayout* form, const QString& label, bool directory);
    QSpinBox* createSpinRow(const QString& label, int min, int max, int value);
    QDoubleSpinBox* createDoubleSpinRow(const QString& label, double min, double max, double value);

    QLineEdit* leftCalibrationEdit_ = nullptr;
    QLineEdit* rightCalibrationEdit_ = nullptr;
    QLineEdit* leftReconstructionEdit_ = nullptr;
    QLineEdit* rightReconstructionEdit_ = nullptr;
    QLineEdit* calibrationFileEdit_ = nullptr;
    QLineEdit* outputDirectoryEdit_ = nullptr;

    QSpinBox* boardWidthSpin_ = nullptr;
    QSpinBox* boardHeightSpin_ = nullptr;
    QDoubleSpinBox* squareWidthSpin_ = nullptr;
    QDoubleSpinBox* squareHeightSpin_ = nullptr;
    QSpinBox* imageBeginSpin_ = nullptr;
    QSpinBox* imageEndSpin_ = nullptr;

    QComboBox* laserModeCombo_ = nullptr;
    QComboBox* laserColorCombo_ = nullptr;
    QSpinBox* leftRoiXSpin_ = nullptr;
    QSpinBox* leftRoiYSpin_ = nullptr;
    QSpinBox* leftRoiWSpin_ = nullptr;
    QSpinBox* leftRoiHSpin_ = nullptr;
    QSpinBox* rightRoiXSpin_ = nullptr;
    QSpinBox* rightRoiYSpin_ = nullptr;
    QSpinBox* rightRoiWSpin_ = nullptr;
    QSpinBox* rightRoiHSpin_ = nullptr;
    QSpinBox* grayThresholdSpin_ = nullptr;
    QSpinBox* minGraySpin_ = nullptr;
    QDoubleSpinBox* binaryThresholdSpin_ = nullptr;
    QDoubleSpinBox* selectionThresholdSpin_ = nullptr;
    QDoubleSpinBox* stripeWidthSpin_ = nullptr;
    QDoubleSpinBox* matchDistanceSpin_ = nullptr;
    QCheckBox* removeEndpointsCheck_ = nullptr;
    QSpinBox* removeEndpointCountSpin_ = nullptr;
};

} // namespace htmsr::app
