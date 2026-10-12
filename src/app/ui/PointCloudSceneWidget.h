#pragma once

#include "core/PointCloudProcessingService.h"
#include <QColor>
#include <QPolygon>
#include <QWidget>
#include <functional>
#include <memory>

namespace htmsr::app
{

enum class CloudSelectionTool
{
    Browse,
    Rectangle,
    Ellipse,
    Lasso,
    Polygon
};
struct CloudSceneLayer
{
    const CloudPoints* points = nullptr;
    const std::vector<bool>* selection = nullptr;
    const std::vector<double>* errors = nullptr;
    QColor color = QColor(35, 50, 65);
};

class PointCloudSceneWidget final : public QWidget
{
public:
    explicit PointCloudSceneWidget(QWidget* parent = nullptr);
    ~PointCloudSceneWidget() override;
    void setLayers(const std::vector<CloudSceneLayer>& layers, const std::vector<CloudFitResult>& fits,
        double errorRange, bool resetCamera);
    void setSelectionTool(CloudSelectionTool tool);
    void setPointSize(double size);
    void focus(const Eigen::Vector3d& center);
    Eigen::Matrix4d viewProjection() const;
    void setSelectionHandler(std::function<void(const QPolygon&, Qt::KeyboardModifiers)> handler);
    QSize viewportSize() const;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace htmsr::app
