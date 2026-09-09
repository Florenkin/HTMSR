#pragma once

#include <Eigen/Core>
#include <QWidget>

#include <memory>
#include <vector>

namespace htmsr::app {

class PointCloudViewWidget final : public QWidget {
    Q_OBJECT

public:
    /*
        函数功能：构造点云显示控件，VTK 可用时内嵌三维视图，否则显示占位视图
        输入：
            parent：Qt 父控件
        输出：
            无
    */
    explicit PointCloudViewWidget(QWidget* parent = nullptr);
    ~PointCloudViewWidget() override;

public slots:
    /*
        函数功能：显示或更新当前三维点云
        输入：
            points：待显示的三维点集合
        输出：
            无
    */
    void setPoints(const std::vector<Eigen::Vector3d>& points);
    // 清空当前点云显示。
    void clear();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace htmsr::app
