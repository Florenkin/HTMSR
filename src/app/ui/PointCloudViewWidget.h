#pragma once

#include <Eigen/Core>
#include <QWidget>

#include <memory>
#include <vector>

namespace htmsr::app {

class PointCloudViewWidget final : public QWidget {
    Q_OBJECT

public:
    explicit PointCloudViewWidget(QWidget* parent = nullptr);
    ~PointCloudViewWidget() override;

public slots:
    void setPoints(const std::vector<Eigen::Vector3d>& points);
    void clear();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace htmsr::app
