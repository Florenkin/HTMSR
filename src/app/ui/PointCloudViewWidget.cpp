#include "app/ui/PointCloudViewWidget.h"

#include <QLabel>
#include <QVBoxLayout>

#if HTMSR_WITH_VTK_VIEWER
#include <QVTKOpenGLNativeWidget.h>
#include <cstdint>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#endif

namespace htmsr::app {

class PointCloudViewWidget::Impl {
public:
#if HTMSR_WITH_VTK_VIEWER
    QVTKOpenGLNativeWidget* widget = nullptr;
    pcl::visualization::PCLVisualizer::Ptr viewer;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow;
    vtkSmartPointer<vtkRenderer> renderer;
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
    // VTK Qt 组件可用时，内嵌 PCLVisualizer 作为点云交互视图。
    impl_->widget = new QVTKOpenGLNativeWidget(this);
    impl_->renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    impl_->widget->setRenderWindow(impl_->renderWindow);

    impl_->renderer = vtkSmartPointer<vtkRenderer>::New();
    impl_->viewer = pcl::visualization::PCLVisualizer::Ptr(
        new pcl::visualization::PCLVisualizer(
            impl_->renderer,
            impl_->renderWindow,
            "HTMSR Viewer",
            false));
    impl_->viewer->setupInteractor(impl_->widget->interactor(), impl_->widget->renderWindow());
    impl_->viewer->setBackgroundColor(0.78, 0.78, 0.78);
    impl_->viewer->addCoordinateSystem(30.0);
    layout->addWidget(impl_->widget);
#else
    // 当前环境缺少 VTK Qt 组件时使用占位视图，保证其他功能仍可运行。
    impl_->placeholder = new QLabel(QString::fromUtf8("点云视图需要 VTK Qt 组件。当前构建使用占位视图。"));
    impl_->placeholder->setAlignment(Qt::AlignCenter);
    impl_->placeholder->setStyleSheet("background:#9a9a9a;color:#202020;");
    layout->addWidget(impl_->placeholder);
#endif
}

PointCloudViewWidget::~PointCloudViewWidget() = default;

void PointCloudViewWidget::setPoints(const std::vector<Eigen::Vector3d>& points)
{
#if HTMSR_WITH_VTK_VIEWER
    // 将 Eigen 点集合转换为 PCL 点云，并刷新三维视图。
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
    cloud->height = 1;
    cloud->width = static_cast<std::uint32_t>(points.size());
    cloud->is_dense = true;
    cloud->points.reserve(points.size());
    for (const auto& point : points) {
        cloud->points.emplace_back(
            static_cast<float>(point.x()),
            static_cast<float>(point.y()),
            static_cast<float>(point.z()));
    }

    impl_->viewer->removePointCloud("cloud");
    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> color(cloud, 20, 20, 20);
    impl_->viewer->addPointCloud(cloud, color, "cloud");
    impl_->viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 1.0, "cloud");
    impl_->viewer->resetCamera();
    impl_->widget->renderWindow()->Render();
#else
    // 占位模式下至少显示点云数量，方便验证重建流程是否产生结果。
    impl_->placeholder->setText(QString::fromUtf8("点云数量: %1\nVTK Qt 组件未启用").arg(points.size()));
#endif
}

void PointCloudViewWidget::clear()
{
#if HTMSR_WITH_VTK_VIEWER
    impl_->viewer->removePointCloud("cloud");
    impl_->widget->renderWindow()->Render();
#else
    impl_->placeholder->setText(QString::fromUtf8("点云视图需要 VTK Qt 组件。当前构建使用占位视图。"));
#endif
}

} // namespace htmsr::app
