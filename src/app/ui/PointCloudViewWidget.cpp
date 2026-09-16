#include "app/ui/PointCloudViewWidget.h"

#include "core/Logger.h"
#include "core/PointCloudService.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

#if HTMSR_WITH_VTK_VIEWER
#include <QVTKOpenGLNativeWidget.h>
#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkVertexGlyphFilter.h>
#endif

namespace htmsr::app {
namespace {

constexpr size_t kMaxDisplayPointCount = 300000;

struct PointCloudEntry {
    int id = 0;
    QString name;
    QString source;
    std::vector<Eigen::Vector3d> points;
    Eigen::Vector3d minimum = Eigen::Vector3d::Zero();
    Eigen::Vector3d maximum = Eigen::Vector3d::Zero();
};

void calculateBounds(PointCloudEntry& entry)
{
    if (entry.points.empty()) {
        entry.minimum = Eigen::Vector3d::Zero();
        entry.maximum = Eigen::Vector3d::Zero();
        return;
    }
    entry.minimum = entry.points.front();
    entry.maximum = entry.points.front();
    for (const auto& point : entry.points) {
        entry.minimum = entry.minimum.cwiseMin(point);
        entry.maximum = entry.maximum.cwiseMax(point);
    }
}

std::vector<Eigen::Vector3d> samplePointsForDisplay(const std::vector<Eigen::Vector3d>& points)
{
    if (points.size() <= kMaxDisplayPointCount) {
        return points;
    }

    std::vector<Eigen::Vector3d> sampled;
    sampled.reserve(kMaxDisplayPointCount);
    const double step = static_cast<double>(points.size()) / static_cast<double>(kMaxDisplayPointCount);
    for (size_t i = 0; i < kMaxDisplayPointCount; ++i) {
        const size_t sourceIndex = std::min(
            static_cast<size_t>(static_cast<double>(i) * step),
            points.size() - 1);
        sampled.push_back(points[sourceIndex]);
    }
    return sampled;
}

#if !HTMSR_WITH_VTK_VIEWER
class QtPointCloudCanvas final : public QWidget {
public:
    explicit QtPointCloudCanvas(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(320, 240);
        setFocusPolicy(Qt::StrongFocus);
    }

    void setPoints(const std::vector<Eigen::Vector3d>& newPoints)
    {
        totalPointCount_ = newPoints.size();
        points_ = samplePointsForDisplay(newPoints);
        updateBounds();
        update();
    }

    void clear()
    {
        points_.clear();
        totalPointCount_ = 0;
        updateBounds();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(154, 154, 154));
        painter.setRenderHint(QPainter::Antialiasing, false);

        if (points_.empty()) {
            painter.setPen(QColor(30, 30, 30));
            painter.drawText(rect(), Qt::AlignCenter, QString::fromUtf8("当前没有点云"));
            return;
        }

        const double viewScale = baseScale_ * zoom_;
        const double cy = std::cos(yaw_);
        const double sy = std::sin(yaw_);
        const double cp = std::cos(pitch_);
        const double sp = std::sin(pitch_);
        const QPointF screenCenter(width() * 0.5 + pan_.x(), height() * 0.5 + pan_.y());

        struct ProjectedPoint {
            QPointF position;
            double depth = 0.0;
        };

        std::vector<ProjectedPoint> projected;
        projected.reserve(points_.size());
        double minDepth = std::numeric_limits<double>::max();
        double maxDepth = std::numeric_limits<double>::lowest();

        for (const auto& point : points_) {
            const Eigen::Vector3d centered = point - center_;
            const double x1 = cy * centered.x() + sy * centered.z();
            const double z1 = -sy * centered.x() + cy * centered.z();
            const double y2 = cp * centered.y() - sp * z1;
            const double z2 = sp * centered.y() + cp * z1;

            ProjectedPoint projectedPoint;
            projectedPoint.position = QPointF(
                screenCenter.x() + x1 * viewScale,
                screenCenter.y() - y2 * viewScale);
            projectedPoint.depth = z2;
            projected.push_back(projectedPoint);
            minDepth = std::min(minDepth, z2);
            maxDepth = std::max(maxDepth, z2);
        }

        std::sort(projected.begin(), projected.end(), [](const ProjectedPoint& lhs, const ProjectedPoint& rhs) {
            return lhs.depth < rhs.depth;
        });

        const double depthRange = std::max(1e-9, maxDepth - minDepth);
        for (const auto& point : projected) {
            const double normalizedDepth = (point.depth - minDepth) / depthRange;
            const int shade = static_cast<int>(65 + normalizedDepth * 165);
            painter.setPen(QColor(shade, shade, shade));
            painter.drawPoint(point.position);
        }

        painter.setPen(QColor(20, 20, 20));
        painter.drawText(
            12,
            22,
            QString::fromUtf8("点云数量: %1 | 显示: %2 | 左键旋转，滚轮缩放，右键平移，双击复位")
                .arg(static_cast<qulonglong>(totalPointCount_))
                .arg(static_cast<qulonglong>(points_.size())));
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        lastMousePosition_ = event->pos();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const QPoint delta = event->pos() - lastMousePosition_;
        lastMousePosition_ = event->pos();

        if (event->buttons() & Qt::LeftButton) {
            yaw_ += static_cast<double>(delta.x()) * 0.01;
            pitch_ += static_cast<double>(delta.y()) * 0.01;
            const double pitchLimit = 1.55;
            pitch_ = std::max(-pitchLimit, std::min(pitchLimit, pitch_));
            update();
        } else if (event->buttons() & Qt::RightButton) {
            pan_ += delta;
            update();
        }
    }

    void wheelEvent(QWheelEvent* event) override
    {
        const double steps = static_cast<double>(event->angleDelta().y()) / 120.0;
        zoom_ *= std::pow(1.15, steps);
        zoom_ = std::max(0.05, std::min(zoom_, 100.0));
        update();
    }

    void mouseDoubleClickEvent(QMouseEvent*) override
    {
        yaw_ = -0.6;
        pitch_ = 0.35;
        zoom_ = 1.0;
        pan_ = QPointF(0.0, 0.0);
        update();
    }

    void resizeEvent(QResizeEvent*) override
    {
        updateBounds();
    }

private:
    void updateBounds()
    {
        if (points_.empty()) {
            center_ = Eigen::Vector3d::Zero();
            baseScale_ = 1.0;
            return;
        }

        Eigen::Vector3d minPoint(
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max());
        Eigen::Vector3d maxPoint(
            std::numeric_limits<double>::lowest(),
            std::numeric_limits<double>::lowest(),
            std::numeric_limits<double>::lowest());

        for (const auto& point : points_) {
            minPoint = minPoint.cwiseMin(point);
            maxPoint = maxPoint.cwiseMax(point);
        }

        center_ = (minPoint + maxPoint) * 0.5;
        const Eigen::Vector3d extent = maxPoint - minPoint;
        const double maxExtent = std::max({ extent.x(), extent.y(), extent.z(), 1.0 });
        const double viewportSize = static_cast<double>(std::max(1, std::min(width(), height())));
        baseScale_ = viewportSize * 0.72 / maxExtent;
    }

    std::vector<Eigen::Vector3d> points_;
    size_t totalPointCount_ = 0;
    Eigen::Vector3d center_ = Eigen::Vector3d::Zero();
    double baseScale_ = 1.0;
    double yaw_ = -0.6;
    double pitch_ = 0.35;
    double zoom_ = 1.0;
    QPointF pan_;
    QPoint lastMousePosition_;
};
#endif

} // namespace

class PointCloudViewWidget::Impl {
public:
    QVBoxLayout* previewListLayout = nullptr;
    QLabel* emptyListLabel = nullptr;
    QLabel* sourceLabel = nullptr;
    QLabel* pointCountLabel = nullptr;
    QLabel* minimumLabel = nullptr;
    QLabel* maximumLabel = nullptr;
    QLabel* extentLabel = nullptr;
    std::vector<PointCloudEntry> clouds;
    htmsr::PointCloudService service;
    int selectedId = -1;
    int nextId = 1;
    int nextReconstructionNumber = 1;
#if HTMSR_WITH_VTK_VIEWER
    QVTKOpenGLNativeWidget* widget = nullptr;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow;
    vtkSmartPointer<vtkRenderer> renderer;
    vtkSmartPointer<vtkPoints> points;
    vtkSmartPointer<vtkPolyData> polyData;
    vtkSmartPointer<vtkVertexGlyphFilter> glyphFilter;
    vtkSmartPointer<vtkPolyDataMapper> mapper;
    vtkSmartPointer<vtkActor> actor;
    vtkSmartPointer<vtkInteractorStyleTrackballCamera> interactionStyle;
#else
    QtPointCloudCanvas* canvas = nullptr;
#endif
};

PointCloudViewWidget::PointCloudViewWidget(QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>())
{
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    rootLayout->addWidget(splitter);

    auto* previewPage = new QWidget;
    auto* previewLayout = new QVBoxLayout(previewPage);
    previewLayout->setContentsMargins(8, 8, 8, 8);
    auto* previewHeader = new QHBoxLayout;
    previewHeader->addWidget(new QLabel(QString::fromUtf8("点云预览栏")), 1);
    auto* loadButton = new QPushButton(QString::fromUtf8("加载点云"));
    previewHeader->addWidget(loadButton);
    previewLayout->addLayout(previewHeader);

    auto* listContainer = new QWidget;
    impl_->previewListLayout = new QVBoxLayout(listContainer);
    impl_->previewListLayout->setContentsMargins(4, 4, 4, 4);
    impl_->previewListLayout->setSpacing(6);
    auto* listScroll = new QScrollArea;
    listScroll->setWidgetResizable(true);
    listScroll->setWidget(listContainer);
    listScroll->setMinimumWidth(410);
    previewLayout->addWidget(listScroll, 1);

    auto* detailPage = new QWidget;
    auto* detailLayout = new QVBoxLayout(detailPage);
    detailLayout->setContentsMargins(8, 8, 8, 8);

    auto* informationGroup = new QGroupBox(QString::fromUtf8("点云详细信息"));
    auto* informationForm = new QFormLayout(informationGroup);
    impl_->sourceLabel = new QLabel;
    impl_->sourceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    impl_->sourceLabel->setWordWrap(true);
    impl_->pointCountLabel = new QLabel;
    impl_->minimumLabel = new QLabel;
    impl_->maximumLabel = new QLabel;
    impl_->extentLabel = new QLabel;
    informationForm->addRow(QString::fromUtf8("来源"), impl_->sourceLabel);
    informationForm->addRow(QString::fromUtf8("点数"), impl_->pointCountLabel);
    informationForm->addRow(QString::fromUtf8("最小坐标"), impl_->minimumLabel);
    informationForm->addRow(QString::fromUtf8("最大坐标"), impl_->maximumLabel);
    informationForm->addRow(QString::fromUtf8("尺寸 X/Y/Z"), impl_->extentLabel);
#if HTMSR_WITH_VTK_VIEWER
    impl_->renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    impl_->renderer = vtkSmartPointer<vtkRenderer>::New();
    impl_->renderWindow->AddRenderer(impl_->renderer);

    impl_->widget = new QVTKOpenGLNativeWidget(this);
    impl_->widget->setRenderWindow(impl_->renderWindow);
    impl_->interactionStyle = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
    impl_->widget->interactor()->SetInteractorStyle(impl_->interactionStyle);

    impl_->points = vtkSmartPointer<vtkPoints>::New();
    impl_->polyData = vtkSmartPointer<vtkPolyData>::New();
    impl_->polyData->SetPoints(impl_->points);

    impl_->glyphFilter = vtkSmartPointer<vtkVertexGlyphFilter>::New();
    impl_->glyphFilter->SetInputData(impl_->polyData);
    impl_->glyphFilter->Update();

    impl_->mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    impl_->mapper->SetInputConnection(impl_->glyphFilter->GetOutputPort());

    impl_->actor = vtkSmartPointer<vtkActor>::New();
    impl_->actor->SetMapper(impl_->mapper);
    impl_->actor->GetProperty()->SetColor(0.05, 0.05, 0.05);
    impl_->actor->GetProperty()->SetPointSize(2.0);

    impl_->renderer->AddActor(impl_->actor);
    impl_->renderer->SetBackground(0.78, 0.78, 0.78);
    detailLayout->addWidget(impl_->widget, 1);
#else
    impl_->canvas = new QtPointCloudCanvas(this);
    detailLayout->addWidget(impl_->canvas, 1);
#endif
    detailLayout->addWidget(informationGroup);

    splitter->addWidget(previewPage);
    splitter->addWidget(detailPage);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 440, 1000 });

    connect(loadButton, &QPushButton::clicked, this, &PointCloudViewWidget::loadPointCloud);
    rebuildPreviewList();
    updateDetail();
}

PointCloudViewWidget::~PointCloudViewWidget()
{
#if HTMSR_WITH_VTK_VIEWER
    if (impl_->widget) {
        if (auto* interactor = impl_->widget->interactor()) {
            interactor->SetRenderWindow(nullptr);
        }
        impl_->widget->setRenderWindow(static_cast<vtkGenericOpenGLRenderWindow*>(nullptr));
    }

    if (impl_->renderer && impl_->actor) {
        impl_->renderer->RemoveActor(impl_->actor);
    }
    if (impl_->renderWindow && impl_->renderer) {
        impl_->renderWindow->RemoveRenderer(impl_->renderer);
    }
    impl_->actor = nullptr;
    impl_->mapper = nullptr;
    impl_->glyphFilter = nullptr;
    impl_->polyData = nullptr;
    impl_->points = nullptr;
    impl_->interactionStyle = nullptr;
    impl_->renderer = nullptr;
    impl_->renderWindow = nullptr;
#endif
}

void PointCloudViewWidget::setPoints(const std::vector<Eigen::Vector3d>& points)
{
    if (points.empty()) {
        return;
    }
    addPointCloud(
        points,
        QString::fromUtf8("重建点云 %1").arg(impl_->nextReconstructionNumber++),
        QString::fromUtf8("软件重建结果"));
}

void PointCloudViewWidget::addPointCloud(
    std::vector<Eigen::Vector3d> points,
    const QString& name,
    const QString& source)
{
    PointCloudEntry entry;
    entry.id = impl_->nextId++;
    entry.name = name;
    entry.source = source;
    entry.points = std::move(points);
    calculateBounds(entry);
    impl_->clouds.push_back(std::move(entry));
    impl_->selectedId = impl_->clouds.back().id;
    rebuildPreviewList();
    updateDetail();
}

void PointCloudViewWidget::renderPoints(const std::vector<Eigen::Vector3d>& points)
{
#if HTMSR_WITH_VTK_VIEWER
    const auto displayPoints = samplePointsForDisplay(points);
    auto newPoints = vtkSmartPointer<vtkPoints>::New();
    newPoints->SetDataTypeToFloat();
    newPoints->Allocate(static_cast<vtkIdType>(displayPoints.size()));
    for (const auto& point : displayPoints) {
        newPoints->InsertNextPoint(
            static_cast<float>(point.x()),
            static_cast<float>(point.y()),
            static_cast<float>(point.z()));
    }

    impl_->points = newPoints;
    impl_->polyData->SetPoints(impl_->points);
    impl_->polyData->Modified();
    impl_->glyphFilter->Update();
    impl_->renderer->ResetCamera();
    impl_->renderWindow->Render();
#else
    impl_->canvas->setPoints(points);
#endif
}

void PointCloudViewWidget::clearRenderer()
{
#if HTMSR_WITH_VTK_VIEWER
    impl_->points = vtkSmartPointer<vtkPoints>::New();
    impl_->polyData->SetPoints(impl_->points);
    impl_->polyData->Modified();
    impl_->glyphFilter->Update();
    impl_->renderWindow->Render();
#else
    impl_->canvas->clear();
#endif
}

void PointCloudViewWidget::clear()
{
    impl_->clouds.clear();
    impl_->selectedId = -1;
    rebuildPreviewList();
    updateDetail();
}

void PointCloudViewWidget::loadPointCloud()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        QString::fromUtf8("加载点云"),
        QString(),
        QString::fromUtf8("点云文件 (*.pcd *.txt *.xyz);;PCD (*.pcd);;XYZ 文本 (*.txt *.xyz);;所有文件 (*.*)"));
    if (file.isEmpty()) {
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        auto points = impl_->service.load(file.toStdString());
        QApplication::restoreOverrideCursor();
        const QFileInfo information(file);
        addPointCloud(std::move(points), information.completeBaseName(), information.absoluteFilePath());
    } catch (const std::exception& ex) {
        QApplication::restoreOverrideCursor();
        Logger::instance().error("PointCloud", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("点云加载失败"), QString::fromUtf8(ex.what()));
    }
}

void PointCloudViewWidget::renamePointCloud(int id)
{
    const auto iterator = std::find_if(impl_->clouds.begin(), impl_->clouds.end(), [id](const PointCloudEntry& entry) {
        return entry.id == id;
    });
    if (iterator == impl_->clouds.end()) {
        return;
    }

    bool accepted = false;
    const QString name = QInputDialog::getText(
        this,
        QString::fromUtf8("重命名点云"),
        QString::fromUtf8("点云名称"),
        QLineEdit::Normal,
        iterator->name,
        &accepted).trimmed();
    if (!accepted || name.isEmpty()) {
        return;
    }
    iterator->name = name;
    rebuildPreviewList();
    updateDetail();
}

void PointCloudViewWidget::removePointCloud(int id)
{
    const auto iterator = std::find_if(impl_->clouds.begin(), impl_->clouds.end(), [id](const PointCloudEntry& entry) {
        return entry.id == id;
    });
    if (iterator == impl_->clouds.end()) {
        return;
    }
    const auto answer = QMessageBox::question(
        this,
        QString::fromUtf8("删除点云"),
        QString::fromUtf8("确定从预览栏移除“%1”吗？\n本操作不会删除磁盘上的源文件。").arg(iterator->name),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    const int removedIndex = static_cast<int>(std::distance(impl_->clouds.begin(), iterator));
    impl_->clouds.erase(iterator);
    if (impl_->selectedId == id) {
        if (impl_->clouds.empty()) {
            impl_->selectedId = -1;
        } else {
            const int nextIndex = std::min(removedIndex, static_cast<int>(impl_->clouds.size()) - 1);
            impl_->selectedId = impl_->clouds[static_cast<size_t>(nextIndex)].id;
        }
    }
    rebuildPreviewList();
    updateDetail();
}

void PointCloudViewWidget::selectPointCloud(int id)
{
    const auto iterator = std::find_if(impl_->clouds.begin(), impl_->clouds.end(), [id](const PointCloudEntry& entry) {
        return entry.id == id;
    });
    if (iterator == impl_->clouds.end()) {
        return;
    }
    impl_->selectedId = id;
    rebuildPreviewList();
    updateDetail();
}

void PointCloudViewWidget::rebuildPreviewList()
{
    while (QLayoutItem* item = impl_->previewListLayout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    if (impl_->clouds.empty()) {
        auto* emptyLabel = new QLabel(QString::fromUtf8("暂无点云，请加载本地文件或执行三维重建。"));
        emptyLabel->setWordWrap(true);
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet(QStringLiteral("color: #666666; padding: 24px;"));
        impl_->previewListLayout->addWidget(emptyLabel);
        impl_->previewListLayout->addStretch(1);
        return;
    }

    for (size_t index = 0; index < impl_->clouds.size(); ++index) {
        const PointCloudEntry& entry = impl_->clouds[index];
        auto* row = new QFrame;
        row->setFrameShape(QFrame::StyledPanel);
        row->setStyleSheet(entry.id == impl_->selectedId
                ? QStringLiteral("QFrame { background: #dbeafe; border: 1px solid #5b9bd5; }")
                : QStringLiteral("QFrame { background: #ffffff; border: 1px solid #c8c8c8; }"));
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(6, 5, 6, 5);
        auto* indexLabel = new QLabel(QString::number(index + 1));
        indexLabel->setAlignment(Qt::AlignCenter);
        indexLabel->setFixedWidth(28);
        rowLayout->addWidget(indexLabel);

        auto* nameButton = new QPushButton(entry.name);
        nameButton->setFlat(true);
        nameButton->setStyleSheet(QStringLiteral("text-align: left; padding: 4px;"));
        nameButton->setToolTip(entry.source);
        rowLayout->addWidget(nameButton, 1);
        auto* renameButton = new QPushButton(QString::fromUtf8("重命名"));
        auto* deleteButton = new QPushButton(QString::fromUtf8("删除"));
        rowLayout->addWidget(renameButton);
        rowLayout->addWidget(deleteButton);

        connect(nameButton, &QPushButton::clicked, this, [this, id = entry.id]() { selectPointCloud(id); });
        connect(renameButton, &QPushButton::clicked, this, [this, id = entry.id]() { renamePointCloud(id); });
        connect(deleteButton, &QPushButton::clicked, this, [this, id = entry.id]() { removePointCloud(id); });
        impl_->previewListLayout->addWidget(row);
    }
    impl_->previewListLayout->addStretch(1);
}

void PointCloudViewWidget::updateDetail()
{
    const auto iterator = std::find_if(impl_->clouds.begin(), impl_->clouds.end(), [this](const PointCloudEntry& entry) {
        return entry.id == impl_->selectedId;
    });
    if (iterator == impl_->clouds.end()) {
        impl_->sourceLabel->setText(QString::fromUtf8("—"));
        impl_->pointCountLabel->setText(QStringLiteral("0"));
        impl_->minimumLabel->setText(QString::fromUtf8("—"));
        impl_->maximumLabel->setText(QString::fromUtf8("—"));
        impl_->extentLabel->setText(QString::fromUtf8("—"));
        clearRenderer();
        return;
    }

    const auto coordinateText = [](const Eigen::Vector3d& point) {
        return QStringLiteral("%1, %2, %3")
            .arg(point.x(), 0, 'f', 4)
            .arg(point.y(), 0, 'f', 4)
            .arg(point.z(), 0, 'f', 4);
    };
    impl_->sourceLabel->setText(iterator->source);
    impl_->pointCountLabel->setText(QString::number(static_cast<qulonglong>(iterator->points.size())));
    impl_->minimumLabel->setText(coordinateText(iterator->minimum));
    impl_->maximumLabel->setText(coordinateText(iterator->maximum));
    impl_->extentLabel->setText(coordinateText(iterator->maximum - iterator->minimum));
    renderPoints(iterator->points);
}

} // namespace htmsr::app
