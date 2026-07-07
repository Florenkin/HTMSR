// 文件说明：
// 声明点云摘要视图，用于展示 PCL 点云的统计信息和样本点。

#pragma once

#include <QLabel>
#include <QTableWidget>
#include <QWidget>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include "data_model/geometry_types.h"

namespace htmsr::visualization {

class PointcloudSummaryWidget : public QWidget {
    Q_OBJECT

public:
    explicit PointcloudSummaryWidget(QWidget* parent = nullptr);

public slots:
    void setPoints(const htmsr::data_model::Point3DList& points);
    void clearView();

private:
    QLabel* summaryLabel_ = nullptr;
    QLabel* boundsLabel_ = nullptr;
    QTableWidget* sampleTable_ = nullptr;
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_ {new pcl::PointCloud<pcl::PointXYZ>()};
};

}  // namespace htmsr::visualization
