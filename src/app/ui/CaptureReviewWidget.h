#pragma once

#include "app/acquisition/AcquisitionTypes.h"

#include <QList>
#include <QWidget>

class QLabel;
class QFrame;
class QPixmap;
class QPushButton;
class QScrollArea;
class QString;
class QVBoxLayout;

namespace htmsr::app {

class ZoomableImageView;

class CaptureReviewWidget final : public QWidget {
    Q_OBJECT

public:
    explicit CaptureReviewWidget(QWidget* parent = nullptr);

    void setCaptureResult(const AcquisitionSessionResult& result);
    const AcquisitionSessionResult& captureResult() const;

signals:
    void captureResultChanged();

private:
    int frameCount() const;
    void rebuildPreviewList();
    void addPreviewRow(int frameIndex);
    void updateRowSelection(int previousFrameIndex, int currentFrameIndex);
    void selectFrame(int frameIndex, bool showLeft);
    void switchDetailSide();
    void deleteFrame(int frameIndex);
    QString imagePathForFrame(int frameIndex, bool leftSide) const;
    QPixmap loadThumbnail(const QString& imagePath);
    void refreshVisibleThumbnails();
    void refreshThumbnailLabel(QLabel* label);
    void updateDetail();

    AcquisitionSessionResult result_;
    QScrollArea* previewScrollArea_ = nullptr;
    QWidget* previewContainer_ = nullptr;
    QVBoxLayout* previewLayout_ = nullptr;
    QList<QFrame*> previewRows_;
    QList<QLabel*> thumbnailLabels_;
    QLabel* detailTitleLabel_ = nullptr;
    QPushButton* switchButton_ = nullptr;
    ZoomableImageView* detailImageView_ = nullptr;
    int selectedFrameIndex_ = -1;
    bool showingLeft_ = true;
};

} // namespace htmsr::app
