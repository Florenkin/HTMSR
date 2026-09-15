#include "app/ui/PointCloudViewWidget.h"

#include <QMouseEvent>
#include <QPainter>
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
#if HTMSR_WITH_VTK_VIEWER
    QVTKOpenGLNativeWidget* widget = nullptr;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow;
    vtkSmartPointer<vtkRenderer> renderer;
    vtkSmartPointer<vtkPoints> points;
    vtkSmartPointer<vtkPolyData> polyData;
    vtkSmartPointer<vtkVertexGlyphFilter> glyphFilter;
    vtkSmartPointer<vtkPolyDataMapper> mapper;
    vtkSmartPointer<vtkActor> actor;
#else
    QtPointCloudCanvas* canvas = nullptr;
#endif
};

PointCloudViewWidget::PointCloudViewWidget(QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>())
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

#if HTMSR_WITH_VTK_VIEWER
    impl_->renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    impl_->renderer = vtkSmartPointer<vtkRenderer>::New();
    impl_->renderWindow->AddRenderer(impl_->renderer);

    impl_->widget = new QVTKOpenGLNativeWidget(this);
    impl_->widget->setRenderWindow(impl_->renderWindow);

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
    layout->addWidget(impl_->widget);
#else
    impl_->canvas = new QtPointCloudCanvas(this);
    layout->addWidget(impl_->canvas);
#endif
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
    impl_->renderer = nullptr;
    impl_->renderWindow = nullptr;
#endif
}

void PointCloudViewWidget::setPoints(const std::vector<Eigen::Vector3d>& points)
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

void PointCloudViewWidget::clear()
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

} // namespace htmsr::app
