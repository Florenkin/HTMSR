#pragma once

#include "app/acquisition/AcquisitionTypes.h"

#include <QList>
#include <QString>
#include <QWidget>

class QLabel;
class QFrame;
class QPixmap;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace htmsr::app {

class ZoomableImageView;

class CaptureReviewWidget final : public QWidget {
    Q_OBJECT

public:
    enum class Mode {
        CaptureEditable,
        LaserExtractionReadOnly
    };

    explicit CaptureReviewWidget(Mode mode = Mode::CaptureEditable, QWidget* parent = nullptr);

    void setCaptureResult(const AcquisitionSessionResult& result);
    void setImagePairs(const std::string& sessionDirectory,
        const std::vector<std::string>& leftImagePaths,
        const std::vector<std::string>& rightImagePaths);
    void clearImages();
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
    QString previewTitle_;
    QString detailTitle_;
    QString emptyText_;
    bool allowDeletion_ = true;
    int selectedFrameIndex_ = -1;
    bool showingLeft_ = true;
};

} // namespace htmsr::app
