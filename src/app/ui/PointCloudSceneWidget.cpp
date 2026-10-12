#include "app/ui/PointCloudSceneWidget.h"
#include <Eigen/Geometry>

#include <QKeyEvent>
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
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkMatrix4x4.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkUnsignedCharArray.h>
#include <vtkVertexGlyphFilter.h>
#endif

namespace htmsr::app
{
namespace
{
    constexpr double pi = 3.14159265358979323846;
    QColor errorColor(double error, double range)
    {
        const double value = std::clamp(error / std::max(range, 1e-12), -1.0, 1.0);
        if (value < 0)
            return QColor(0, int(255 * (1 + value)), int(-255 * value));
        if (value < 0.5)
            return QColor(int(value * 510), 255, 0);
        return QColor(255, int(510 * (1 - value)), 0);
    }

    class SelectionOverlay final : public QWidget
    {
    public:
        CloudSelectionTool tool = CloudSelectionTool::Browse;
        std::function<void(const QPolygon&, Qt::KeyboardModifiers)> handler;
        QPolygon polygon;
        QPoint cursor;
        bool drawing = false;
        Qt::KeyboardModifiers modifiers { };
        explicit SelectionOverlay(QWidget* parent)
            : QWidget(parent)
        {
            setAttribute(Qt::WA_TranslucentBackground);
            setMouseTracking(true);
            setFocusPolicy(Qt::StrongFocus);
            setTool(CloudSelectionTool::Browse);
        }
        void setTool(CloudSelectionTool value)
        {
            tool = value;
            polygon.clear();
            drawing = false;
            setAttribute(Qt::WA_TransparentForMouseEvents, tool == CloudSelectionTool::Browse);
            setCursor(tool == CloudSelectionTool::Browse ? Qt::ArrowCursor : Qt::CrossCursor);
            update();
        }
        void finish()
        {
            if (polygon.size() >= 3 && handler)
                handler(polygon, modifiers);
            polygon.clear();
            drawing = false;
            update();
        }

    protected:
        void paintEvent(QPaintEvent*) override
        {
            if (!drawing)
                return;
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(230, 60, 30), 2));
            p.setBrush(QColor(230, 60, 30, 35));
            if (tool == CloudSelectionTool::Rectangle)
                p.drawRect(QRect(polygon.first(), cursor).normalized());
            else if (tool == CloudSelectionTool::Ellipse)
                p.drawEllipse(QRect(polygon.first(), cursor).normalized());
            else
            {
                QPolygon shape = polygon;
                if (tool == CloudSelectionTool::Polygon)
                    shape << cursor;
                p.drawPolygon(shape);
            }
        }
        void mousePressEvent(QMouseEvent* e) override
        {
            if (e->button() == Qt::RightButton)
            {
                polygon.clear();
                drawing = false;
                update();
                return;
            }
            if (e->button() != Qt::LeftButton)
                return;
            setFocus();
            modifiers = e->modifiers();
            cursor = e->pos();
            if (tool != CloudSelectionTool::Polygon || !drawing)
                polygon.clear();
            polygon << cursor;
            drawing = true;
            update();
        }
        void mouseMoveEvent(QMouseEvent* e) override
        {
            cursor = e->pos();
            if (drawing && tool == CloudSelectionTool::Lasso)
                polygon << cursor;
            update();
        }
        void mouseReleaseEvent(QMouseEvent* e) override
        {
            if (!drawing || e->button() != Qt::LeftButton || tool == CloudSelectionTool::Polygon)
                return;
            cursor = e->pos();
            if (tool == CloudSelectionTool::Rectangle)
            {
                const QRect r = QRect(polygon.first(), cursor).normalized();
                polygon = QPolygon(
                    QVector<QPoint> { r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft() });
            }
            else if (tool == CloudSelectionTool::Ellipse)
            {
                const QRectF r = QRect(polygon.first(), cursor).normalized();
                polygon.clear();
                for (int i = 0; i < 64; ++i)
                    polygon << QPoint(int(r.center().x() + r.width() * 0.5 * std::cos(i * 2 * pi / 64)),
                        int(r.center().y() + r.height() * 0.5 * std::sin(i * 2 * pi / 64)));
            }
            finish();
        }
        void mouseDoubleClickEvent(QMouseEvent*) override
        {
            if (tool == CloudSelectionTool::Polygon)
                finish();
        }
        void keyPressEvent(QKeyEvent* e) override
        {
            if (e->key() == Qt::Key_Escape)
            {
                polygon.clear();
                drawing = false;
                update();
            }
            else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
                finish();
        }
    };

    struct DisplayPoint
    {
        Eigen::Vector3d point;
        QColor color;
    };

#if !HTMSR_WITH_VTK_VIEWER
    class CloudCanvas final : public QWidget
    {
    public:
        std::vector<DisplayPoint> points;
        Eigen::Vector3d center = Eigen::Vector3d::Zero();
        double extent = 1, zoom = 1, yaw = -0.6, pitch = 0.35, pointSize = 2;
        QPointF pan;
        QPoint last;
        explicit CloudCanvas(QWidget* parent)
            : QWidget(parent)
        {
            setMinimumSize(320, 240);
        }
        Eigen::Matrix4d matrix() const
        {
            Eigen::Matrix3d rotation;
            rotation = Eigen::AngleAxisd(pitch, Eigen::Vector3d::UnitX()).toRotationMatrix()
                * Eigen::AngleAxisd(yaw, Eigen::Vector3d::UnitY()).toRotationMatrix();
            Eigen::Matrix4d m = Eigen::Matrix4d::Identity();
            const double scale = std::min(width(), height()) * 0.72 / extent * zoom;
            m.block<3, 3>(0, 0) = rotation;
            m.block<3, 1>(0, 3) = -rotation * center;
            m.row(0) *= 2 * scale / std::max(1, width());
            m.row(1) *= 2 * scale / std::max(1, height());
            m(0, 3) += 2 * pan.x() / std::max(1, width());
            m(1, 3) -= 2 * pan.y() / std::max(1, height());
            m.row(2) /= std::max(extent * 10, 1.0);
            return m;
        }

    protected:
        void paintEvent(QPaintEvent*) override
        {
            QPainter p(this);
            p.fillRect(rect(), QColor(220, 224, 229));
            const auto m = matrix();
            for (const auto& item : points)
            {
                const Eigen::Vector4d projected
                    = m * Eigen::Vector4d(item.point.x(), item.point.y(), item.point.z(), 1);
                p.setPen(QPen(item.color, pointSize));
                p.drawPoint(QPointF((projected.x() + 1) * width() / 2, (1 - projected.y()) * height() / 2));
            }
        }
        void mousePressEvent(QMouseEvent* e) override { last = e->pos(); }
        void mouseMoveEvent(QMouseEvent* e) override
        {
            const auto delta = e->pos() - last;
            last = e->pos();
            if (e->buttons() & Qt::LeftButton)
            {
                yaw += delta.x() * 0.01;
                pitch += delta.y() * 0.01;
            }
            if (e->buttons() & Qt::RightButton)
                pan += delta;
            update();
        }
        void wheelEvent(QWheelEvent* e) override
        {
            zoom = std::clamp(zoom * std::pow(1.15, e->angleDelta().y() / 120.0), 0.01, 100.0);
            update();
        }
    };
#endif

    std::vector<DisplayPoint> modelPoints(const std::vector<CloudFitResult>& fits, double extent)
    {
        std::vector<DisplayPoint> output;
        const auto add
            = [&](const Eigen::Vector3d& point) { output.push_back({ point, QColor(0, 180, 210) }); };
        for (const auto& fit : fits)
        {
            if (fit.kind == CloudFitKind::Sphere)
            {
                for (int ring = 0; ring < 3; ++ring)
                    for (int i = 0; i < 240; ++i)
                    {
                        const double a = i * 2 * pi / 240;
                        Eigen::Vector3d p = Eigen::Vector3d::Zero();
                        p[(ring + 1) % 3] = fit.radius * std::cos(a);
                        p[(ring + 2) % 3] = fit.radius * std::sin(a);
                        add(fit.center + p);
                    }
            }
            else if (fit.kind == CloudFitKind::Cylinder)
            {
                const Eigen::Vector3d u = fit.direction.unitOrthogonal(), v = fit.direction.cross(u);
                for (int ring = 0; ring < 3; ++ring)
                    for (int i = 0; i < 240; ++i)
                    {
                        const double a = i * 2 * pi / 240,
                                     t = fit.axisMinimum + (fit.axisMaximum - fit.axisMinimum) * ring / 2;
                        add(fit.center + fit.direction * t
                            + fit.radius * (u * std::cos(a) + v * std::sin(a)));
                    }
            }
            else
            {
                const Eigen::Vector3d u
                    = fit.kind == CloudFitKind::Plane ? fit.direction.unitOrthogonal() : fit.basisU;
                const Eigen::Vector3d v
                    = fit.kind == CloudFitKind::Plane ? fit.direction.cross(u) : fit.basisV;
                for (int line = 0; line <= 10; ++line)
                    for (int i = 0; i <= 100; ++i)
                        for (int swap = 0; swap < 2; ++swap)
                        {
                            const double a = (line / 10.0 - 0.5) * extent, b = (i / 100.0 - 0.5) * extent;
                            const double x = swap ? b : a, y = swap ? a : b;
                            const double z = fit.kind == CloudFitKind::Plane
                                ? 0
                                : PointCloudProcessingService::surfaceHeight(x, y, fit);
                            add(fit.center + x * u + y * v + z * fit.direction);
                        }
            }
        }
        return output;
    }
}

class PointCloudSceneWidget::Impl
{
public:
    SelectionOverlay* overlay = nullptr;
    double pointSize = 2;
#if HTMSR_WITH_VTK_VIEWER
    QVTKOpenGLNativeWidget* widget = nullptr;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> window;
    vtkSmartPointer<vtkRenderer> renderer;
    vtkSmartPointer<vtkActor> actor;
#else
    CloudCanvas* canvas = nullptr;
#endif
};

PointCloudSceneWidget::PointCloudSceneWidget(QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>())
{
    setMinimumSize(320, 240);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
#if HTMSR_WITH_VTK_VIEWER
    impl_->window = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    impl_->renderer = vtkSmartPointer<vtkRenderer>::New();
    impl_->window->AddRenderer(impl_->renderer);
    impl_->widget = new QVTKOpenGLNativeWidget(this);
    impl_->widget->setRenderWindow(impl_->window);
    auto style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
    impl_->widget->interactor()->SetInteractorStyle(style);
    impl_->renderer->SetBackground(0.86, 0.88, 0.9);
    layout->addWidget(impl_->widget);
#else
    impl_->canvas = new CloudCanvas(this);
    layout->addWidget(impl_->canvas);
#endif
    impl_->overlay = new SelectionOverlay(this);
    impl_->overlay->setObjectName("cloudSelectionOverlay");
    impl_->overlay->raise();
}

PointCloudSceneWidget::~PointCloudSceneWidget()
{
#if HTMSR_WITH_VTK_VIEWER
    // Qt 控件在 VTK 智能指针释放之前销毁，避免 QWidget 基类析构再次访问旧窗口。
    delete impl_->widget;
    impl_->widget = nullptr;
    impl_->window->RemoveRenderer(impl_->renderer);
    impl_->renderer = nullptr;
    impl_->window = nullptr;
#endif
}

void PointCloudSceneWidget::setLayers(const std::vector<CloudSceneLayer>& layers,
    const std::vector<CloudFitResult>& fits, double errorRange, bool resetCamera)
{
    std::vector<DisplayPoint> display;
    Eigen::Vector3d minimum = Eigen::Vector3d::Constant(std::numeric_limits<double>::max()),
                    maximum = -minimum;
    std::size_t total = 0;
    for (const auto& layer : layers)
        if (layer.points)
            total += layer.points->size();
    const std::size_t stride = std::max<std::size_t>(1, (total + 299999) / 300000);
    for (const auto& layer : layers)
    {
        if (!layer.points)
            continue;
        for (std::size_t i = 0; i < layer.points->size(); ++i)
        {
            const auto& point = (*layer.points)[i];
            if (!point.allFinite())
                continue;
            minimum = minimum.cwiseMin(point);
            maximum = maximum.cwiseMax(point);
            if (i % stride)
                continue;
            QColor color = layer.color;
            if (layer.errors && layer.errors->size() == layer.points->size())
                color = errorColor((*layer.errors)[i], errorRange);
            if (layer.selection && i < layer.selection->size() && (*layer.selection)[i])
                color = QColor(255, 110, 0);
            display.push_back({ point, color });
        }
    }
    const bool hasPoints = !display.empty();
    const double extent = hasPoints ? std::max(1e-6, (maximum - minimum).maxCoeff()) : 1;
    const auto models = modelPoints(fits, extent);
    display.insert(display.end(), models.begin(), models.end());
#if HTMSR_WITH_VTK_VIEWER
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetDataTypeToDouble();
    auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    colors->SetNumberOfComponents(3);
    colors->SetName("RGB");
    for (const auto& item : display)
    {
        points->InsertNextPoint(item.point.data());
        unsigned char rgb[] = { static_cast<unsigned char>(item.color.red()),
            static_cast<unsigned char>(item.color.green()), static_cast<unsigned char>(item.color.blue()) };
        colors->InsertNextTypedTuple(rgb);
    }
    auto data = vtkSmartPointer<vtkPolyData>::New();
    data->SetPoints(points);
    data->GetPointData()->SetScalars(colors);
    auto glyph = vtkSmartPointer<vtkVertexGlyphFilter>::New();
    glyph->SetInputData(data);
    glyph->Update();
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(glyph->GetOutput());
    mapper->SetColorModeToDirectScalars();
    if (impl_->actor)
        impl_->renderer->RemoveActor(impl_->actor);
    impl_->actor = vtkSmartPointer<vtkActor>::New();
    impl_->actor->SetMapper(mapper);
    impl_->actor->GetProperty()->SetPointSize(impl_->pointSize);
    impl_->renderer->AddActor(impl_->actor);
    if (resetCamera && hasPoints)
        impl_->renderer->ResetCamera();
    impl_->renderer->ResetCameraClippingRange();
    if (isVisible())
        impl_->window->Render();
#else
    impl_->canvas->points = std::move(display);
    if (resetCamera && hasPoints)
    {
        impl_->canvas->center = (minimum + maximum) * 0.5;
        impl_->canvas->extent = extent;
        impl_->canvas->zoom = 1;
        impl_->canvas->pan = { };
    }
    impl_->canvas->update();
#endif
}

void PointCloudSceneWidget::setSelectionTool(CloudSelectionTool tool) { impl_->overlay->setTool(tool); }
void PointCloudSceneWidget::setSelectionHandler(
    std::function<void(const QPolygon&, Qt::KeyboardModifiers)> handler)
{
    impl_->overlay->handler = std::move(handler);
}
void PointCloudSceneWidget::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    impl_->overlay->setGeometry(rect());
    impl_->overlay->raise();
}
QSize PointCloudSceneWidget::viewportSize() const { return size(); }
Eigen::Matrix4d PointCloudSceneWidget::viewProjection() const
{
#if HTMSR_WITH_VTK_VIEWER
    auto* matrix = impl_->renderer->GetActiveCamera()->GetCompositeProjectionTransformMatrix(
        impl_->renderer->GetTiledAspectRatio(), -1, 1);
    Eigen::Matrix4d result;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            result(r, c) = matrix->GetElement(r, c);
    return result;
#else
    return impl_->canvas->matrix();
#endif
}
void PointCloudSceneWidget::focus(const Eigen::Vector3d& center)
{
#if HTMSR_WITH_VTK_VIEWER
    auto* camera = impl_->renderer->GetActiveCamera();
    double current[3], position[3];
    camera->GetFocalPoint(current);
    camera->GetPosition(position);
    for (int i = 0; i < 3; ++i)
        position[i] += center[i] - current[i];
    camera->SetPosition(position);
    camera->SetFocalPoint(center.data());
    impl_->renderer->ResetCameraClippingRange();
    impl_->window->Render();
#else
    impl_->canvas->center = center;
    impl_->canvas->update();
#endif
}
void PointCloudSceneWidget::setPointSize(double size)
{
    impl_->pointSize = size;
#if HTMSR_WITH_VTK_VIEWER
    if (impl_->actor)
        impl_->actor->GetProperty()->SetPointSize(size);
    if (isVisible())
        impl_->window->Render();
#else
    impl_->canvas->pointSize = size;
    impl_->canvas->update();
#endif
}

} // namespace htmsr::app
