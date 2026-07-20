#include "app/ui/PointCloudViewWidget.h"

#include <QLabel>
#include <QVBoxLayout>

#if HTMSR_WITH_VTK_VIEWER
#include <QVTKOpenGLNativeWidget.h>
#include <cstdint>
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
    QLabel* placeholder = nullptr;
#endif
};

PointCloudViewWidget::PointCloudViewWidget(QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>())
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

#if HTMSR_WITH_VTK_VIEWER
    // VTK Qt 组件可用时，内嵌原生 VTK 管线作为点云交互视图。
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
    // 当前环境缺少 VTK Qt 组件时使用占位视图，保证其他功能仍可运行。
    impl_->placeholder = new QLabel(QString::fromUtf8("点云视图需要 VTK Qt 组件。当前构建使用占位视图。"));
    impl_->placeholder->setAlignment(Qt::AlignCenter);
    impl_->placeholder->setStyleSheet("background:#9a9a9a;color:#202020;");
    layout->addWidget(impl_->placeholder);
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
    // 将 Eigen 点集合写入 VTK 点集，并刷新三维视图。
    auto newPoints = vtkSmartPointer<vtkPoints>::New();
    newPoints->SetDataTypeToFloat();
    newPoints->Allocate(static_cast<vtkIdType>(points.size()));
    for (const auto& point : points) {
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
    // 占位模式下至少显示点云数量，方便验证重建流程是否产生结果。
    impl_->placeholder->setText(QString::fromUtf8("点云数量: %1\nVTK Qt 组件未启用").arg(points.size()));
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
    impl_->placeholder->setText(QString::fromUtf8("点云视图需要 VTK Qt 组件。当前构建使用占位视图。"));
#endif
}

} // namespace htmsr::app
