#include "app/ui/CaptureReviewWidget.h"

#include "app/ui/ZoomableImageView.h"

#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPixmapCache>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

namespace htmsr::app {
namespace {

constexpr int kThumbnailWidth = 128;
constexpr int kThumbnailHeight = 88;
constexpr int kThumbnailPreloadMargin = 240;

class PreviewThumbnailLabel final : public QLabel {
public:
    explicit PreviewThumbnailLabel(QWidget* parent = nullptr)
        : QLabel(parent)
    {
    }

    void setDoubleClickHandler(std::function<void()> handler)
    {
        doubleClickHandler_ = std::move(handler);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && doubleClickHandler_) {
            doubleClickHandler_();
            event->accept();
            return;
        }
        QLabel::mouseDoubleClickEvent(event);
    }

private:
    std::function<void()> doubleClickHandler_;
};

QString thumbnailCacheKey(const QString& imagePath)
{
    const QFileInfo fileInfo(imagePath);
    return QStringLiteral("capture-thumbnail:%1:%2:%3")
        .arg(imagePath)
        .arg(fileInfo.size())
        .arg(fileInfo.lastModified().toMSecsSinceEpoch());
}

QLabel* createThumbnailLabel(const QString& title, const QString& imagePath, std::function<void()> doubleClickHandler)
{
    auto* label = new PreviewThumbnailLabel;
    label->setFixedSize(kThumbnailWidth, kThumbnailHeight);
    label->setAlignment(Qt::AlignCenter);
    label->setFrameShape(QFrame::StyledPanel);
    label->setStyleSheet("QLabel { background: #f7f7f7; color: #777777; border: 1px solid #c8c8c8; }");
    label->setDoubleClickHandler(std::move(doubleClickHandler));
    label->setText(title);
    label->setToolTip(imagePath);
    label->setProperty("imagePath", imagePath);
    label->setProperty("thumbnailLoaded", false);
    return label;
}

} // namespace

CaptureReviewWidget::CaptureReviewWidget(Mode mode, QWidget* parent)
    : QWidget(parent)
{
    allowDeletion_ = mode == Mode::CaptureEditable;
    previewTitle_ = allowDeletion_ ? QString::fromUtf8("采集预览栏") : QString::fromUtf8("激光线预览栏");
    detailTitle_ = allowDeletion_ ? QString::fromUtf8("采集详情图") : QString::fromUtf8("激光线详情图");
    emptyText_ = allowDeletion_ ? QString::fromUtf8("暂无采集图像") : QString::fromUtf8("本次未启用激光线图片保存");

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Horizontal);
    rootLayout->addWidget(splitter);

    previewContainer_ = new QWidget;
    previewLayout_ = new QVBoxLayout(previewContainer_);
    previewLayout_->setContentsMargins(8, 8, 8, 8);
    previewLayout_->setSpacing(8);
    previewLayout_->addStretch(1);

    auto* previewPage = new QWidget;
    auto* previewPageLayout = new QVBoxLayout(previewPage);
    previewPageLayout->setContentsMargins(8, 8, 8, 8);
    previewPageLayout->setSpacing(6);

    auto* previewTitleLabel = new QLabel(previewTitle_);

    previewScrollArea_ = new QScrollArea;
    previewScrollArea_->setWidget(previewContainer_);
    previewScrollArea_->setWidgetResizable(true);
    previewScrollArea_->setMinimumWidth(390);
    connect(previewScrollArea_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        refreshVisibleThumbnails();
    });
    previewPageLayout->addWidget(previewTitleLabel);
    previewPageLayout->addWidget(previewScrollArea_, 1);

    auto* detailPage = new QWidget;
    auto* detailLayout = new QVBoxLayout(detailPage);
    detailLayout->setContentsMargins(8, 8, 8, 8);
    detailLayout->setSpacing(6);

    auto* headerLayout = new QHBoxLayout;
    detailTitleLabel_ = new QLabel(detailTitle_);
    detailTitleLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    switchButton_ = new QPushButton(QString::fromUtf8("切换"));
    switchButton_->setEnabled(false);
    headerLayout->addWidget(detailTitleLabel_);
    headerLayout->addWidget(switchButton_);

    detailImageView_ = new ZoomableImageView;
    detailLayout->addLayout(headerLayout);
    detailLayout->addWidget(detailImageView_, 1);

    splitter->addWidget(previewPage);
    splitter->addWidget(detailPage);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    QList<int> splitterSizes;
    splitterSizes << 420 << 900;
    splitter->setSizes(splitterSizes);

    connect(switchButton_, &QPushButton::clicked, this, &CaptureReviewWidget::switchDetailSide);
}

void CaptureReviewWidget::setCaptureResult(const AcquisitionSessionResult& result)
{
    result_ = result;
    result_.capturedFrameCount = frameCount();
    selectedFrameIndex_ = frameCount() > 0 ? 0 : -1;
    showingLeft_ = true;
    rebuildPreviewList();
    updateDetail();
}

void CaptureReviewWidget::setImagePairs(const std::string& sessionDirectory,
    const std::vector<std::string>& leftImagePaths,
    const std::vector<std::string>& rightImagePaths)
{
    if (!allowDeletion_) {
        emptyText_ = QString::fromUtf8("本次没有可显示的激光线图片");
    }
    AcquisitionSessionResult result;
    result.sessionDirectory = sessionDirectory;
    result.leftImagePaths = leftImagePaths;
    result.rightImagePaths = rightImagePaths;
    result.capturedFrameCount = static_cast<int>(std::min(leftImagePaths.size(), rightImagePaths.size()));
    result.success = result.capturedFrameCount > 0;
    setCaptureResult(result);
}

void CaptureReviewWidget::clearImages()
{
    if (!allowDeletion_) {
        emptyText_ = QString::fromUtf8("本次未启用激光线图片保存");
    }
    setCaptureResult({});
}

const AcquisitionSessionResult& CaptureReviewWidget::captureResult() const
{
    return result_;
}

int CaptureReviewWidget::frameCount() const
{
    return static_cast<int>(std::min(result_.leftImagePaths.size(), result_.rightImagePaths.size()));
}

void CaptureReviewWidget::rebuildPreviewList()
{
    previewRows_.clear();
    thumbnailLabels_.clear();
    while (auto* item = previewLayout_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    if (frameCount() <= 0) {
        auto* emptyLabel = new QLabel(emptyText_);
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setMinimumHeight(120);
        emptyLabel->setStyleSheet("QLabel { color: #777777; }");
        previewLayout_->addWidget(emptyLabel);
    } else {
        for (int frameIndex = 0; frameIndex < frameCount(); ++frameIndex) {
            addPreviewRow(frameIndex);
        }
    }

    previewLayout_->addStretch(1);
    QTimer::singleShot(0, this, &CaptureReviewWidget::refreshVisibleThumbnails);
}

void CaptureReviewWidget::addPreviewRow(int frameIndex)
{
    auto* row = new QFrame;
    row->setFrameShape(QFrame::StyledPanel);
    row->setStyleSheet(frameIndex == selectedFrameIndex_
            ? "QFrame { background: #eef5ff; border: 1px solid #8ab4e8; }"
            : "QFrame { background: #ffffff; border: 1px solid #c8c8c8; }");

    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(8, 6, 8, 6);
    rowLayout->setSpacing(8);

    auto* frameLabel = new QLabel(QString::number(frameIndex + 1));
    frameLabel->setAlignment(Qt::AlignCenter);
    frameLabel->setMinimumWidth(28);

    auto* leftLabel = createThumbnailLabel(QString::fromUtf8("左图"), imagePathForFrame(frameIndex, true), [this, frameIndex]() {
        selectFrame(frameIndex, true);
    });
    auto* rightLabel = createThumbnailLabel(QString::fromUtf8("右图"), imagePathForFrame(frameIndex, false), [this, frameIndex]() {
        selectFrame(frameIndex, false);
    });

    rowLayout->addWidget(frameLabel);
    rowLayout->addWidget(leftLabel);
    rowLayout->addWidget(rightLabel);
    if (allowDeletion_) {
        auto* buttonColumn = new QWidget;
        auto* buttonLayout = new QVBoxLayout(buttonColumn);
        buttonLayout->setContentsMargins(0, 0, 0, 0);
        buttonLayout->setSpacing(6);
        auto* deleteButton = new QPushButton(QString::fromUtf8("删除"));
        buttonLayout->addWidget(deleteButton);
        buttonLayout->addStretch(1);
        rowLayout->addWidget(buttonColumn);
        connect(deleteButton, &QPushButton::clicked, this, [this, frameIndex]() {
            deleteFrame(frameIndex);
        });
    }

    previewRows_.append(row);
    thumbnailLabels_.append(leftLabel);
    thumbnailLabels_.append(rightLabel);
    previewLayout_->addWidget(row);
}

void CaptureReviewWidget::updateRowSelection(int previousFrameIndex, int currentFrameIndex)
{
    const auto updateStyle = [this](int frameIndex) {
        if (frameIndex < 0 || frameIndex >= previewRows_.size()) {
            return;
        }
        previewRows_[frameIndex]->setStyleSheet(frameIndex == selectedFrameIndex_
                ? "QFrame { background: #eef5ff; border: 1px solid #8ab4e8; }"
                : "QFrame { background: #ffffff; border: 1px solid #c8c8c8; }");
    };

    updateStyle(previousFrameIndex);
    if (currentFrameIndex != previousFrameIndex) {
        updateStyle(currentFrameIndex);
    }
}

void CaptureReviewWidget::selectFrame(int frameIndex, bool showLeft)
{
    if (frameIndex < 0 || frameIndex >= frameCount()) {
        const int previousFrameIndex = selectedFrameIndex_;
        selectedFrameIndex_ = -1;
        updateRowSelection(previousFrameIndex, selectedFrameIndex_);
        updateDetail();
        return;
    }

    const int previousFrameIndex = selectedFrameIndex_;
    selectedFrameIndex_ = frameIndex;
    showingLeft_ = showLeft;
    updateRowSelection(previousFrameIndex, selectedFrameIndex_);
    updateDetail();
}

void CaptureReviewWidget::switchDetailSide()
{
    if (selectedFrameIndex_ < 0) {
        return;
    }

    showingLeft_ = !showingLeft_;
    updateDetail();
}

void CaptureReviewWidget::deleteFrame(int frameIndex)
{
    if (!allowDeletion_ || frameIndex < 0 || frameIndex >= frameCount()) {
        return;
    }

    const auto reply = QMessageBox::question(
        this,
        QString::fromUtf8("删除采集帧"),
        QString::fromUtf8("确定删除第 %1 帧的左右图像吗？").arg(frameIndex + 1));
    if (reply != QMessageBox::Yes) {
        return;
    }

    const QString leftPath = imagePathForFrame(frameIndex, true);
    const QString rightPath = imagePathForFrame(frameIndex, false);
    const auto removeIfPresent = [](const QString& path) {
        return path.isEmpty() || !QFileInfo::exists(path) || QFile::remove(path);
    };

    if (!removeIfPresent(leftPath) || !removeIfPresent(rightPath)) {
        QMessageBox::warning(this, QString::fromUtf8("删除失败"), QString::fromUtf8("部分图像文件无法删除，请检查文件是否被占用。"));
        return;
    }

    result_.leftImagePaths.erase(result_.leftImagePaths.begin() + frameIndex);
    result_.rightImagePaths.erase(result_.rightImagePaths.begin() + frameIndex);
    result_.capturedFrameCount = frameCount();
    result_.success = result_.capturedFrameCount > 0;
    result_.message = "Capture frame deleted, frames=" + std::to_string(result_.capturedFrameCount);

    if (selectedFrameIndex_ == frameIndex) {
        selectedFrameIndex_ = std::min(frameIndex, frameCount() - 1);
    } else if (selectedFrameIndex_ > frameIndex) {
        --selectedFrameIndex_;
    }
    if (frameCount() <= 0) {
        selectedFrameIndex_ = -1;
        showingLeft_ = true;
    }

    rebuildPreviewList();
    updateDetail();
    emit captureResultChanged();
}

QString CaptureReviewWidget::imagePathForFrame(int frameIndex, bool leftSide) const
{
    const auto& paths = leftSide ? result_.leftImagePaths : result_.rightImagePaths;
    if (frameIndex < 0 || frameIndex >= static_cast<int>(paths.size())) {
        return QString();
    }
    return QString::fromStdString(paths[static_cast<size_t>(frameIndex)]);
}

QPixmap CaptureReviewWidget::loadThumbnail(const QString& imagePath)
{
    if (imagePath.isEmpty()) {
        return QPixmap();
    }

    const QString cacheKey = thumbnailCacheKey(imagePath);
    QPixmap pixmap;
    if (QPixmapCache::find(cacheKey, &pixmap)) {
        return pixmap;
    }

    QImageReader reader(imagePath);
    reader.setAutoTransform(true);
    const QSize imageSize = reader.size();
    if (imageSize.isValid()) {
        reader.setScaledSize(imageSize.scaled(kThumbnailWidth, kThumbnailHeight, Qt::KeepAspectRatio));
    }
    pixmap = QPixmap::fromImage(reader.read());
    if (pixmap.isNull()) {
        return QPixmap();
    }
    if (pixmap.width() > kThumbnailWidth || pixmap.height() > kThumbnailHeight) {
        pixmap = pixmap.scaled(kThumbnailWidth, kThumbnailHeight, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    QPixmapCache::insert(cacheKey, pixmap);
    return pixmap;
}

void CaptureReviewWidget::refreshVisibleThumbnails()
{
    if (!previewScrollArea_) {
        return;
    }

    const QRect visibleRect = previewScrollArea_->viewport()->rect();
    for (auto* label : thumbnailLabels_) {
        if (!label || label->property("thumbnailLoaded").toBool()) {
            continue;
        }

        const QPoint labelTopLeft = label->mapTo(previewScrollArea_->viewport(), QPoint(0, 0));
        const QRect labelRect(labelTopLeft, label->size());
        if (visibleRect.adjusted(0, -kThumbnailPreloadMargin, 0, kThumbnailPreloadMargin).intersects(labelRect)) {
            refreshThumbnailLabel(label);
        }
    }
}

void CaptureReviewWidget::refreshThumbnailLabel(QLabel* label)
{
    if (!label) {
        return;
    }

    const QString imagePath = label->property("imagePath").toString();
    const QPixmap pixmap = loadThumbnail(imagePath);
    if (pixmap.isNull()) {
        label->setText(QString::fromUtf8("无图像"));
    } else {
        label->setText(QString());
        label->setPixmap(pixmap);
    }
    label->setProperty("thumbnailLoaded", true);
}

void CaptureReviewWidget::updateDetail()
{
    if (selectedFrameIndex_ < 0 || selectedFrameIndex_ >= frameCount()) {
        detailTitleLabel_->setText(detailTitle_);
        switchButton_->setEnabled(false);
        detailImageView_->clear();
        return;
    }

    const QString sideText = showingLeft_ ? QString::fromUtf8("左图") : QString::fromUtf8("右图");
    detailTitleLabel_->setText(QString::fromUtf8("%1 - 第 %2 帧 %3")
        .arg(detailTitle_).arg(selectedFrameIndex_ + 1).arg(sideText));
    switchButton_->setEnabled(true);
    detailImageView_->setImageFile(imagePathForFrame(selectedFrameIndex_, showingLeft_));
}

} // namespace htmsr::app
