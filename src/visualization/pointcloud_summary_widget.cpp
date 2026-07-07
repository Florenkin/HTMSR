// 文件说明：
// 实现基于 PCL 的点云摘要视图。

#include "visualization/pointcloud_summary_widget.h"

#include <algorithm>

#include <QHeaderView>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace htmsr::visualization {

PointcloudSummaryWidget::PointcloudSummaryWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    summaryLabel_ = new QLabel("暂无点云结果", this);
    boundsLabel_ = new QLabel("边界框: -", this);
    sampleTable_ = new QTableWidget(this);
    sampleTable_->setColumnCount(3);
    sampleTable_->setHorizontalHeaderLabels({"X", "Y", "Z"});
    sampleTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    layout->addWidget(summaryLabel_);
    layout->addWidget(boundsLabel_);
    layout->addWidget(sampleTable_, 1);
}

void PointcloudSummaryWidget::setPoints(const htmsr::data_model::Point3DList& points) {
    // 将项目内部点类型转换为 PCL 点云，便于后续扩展到更完整的三维显示。

    cloud_->clear();
    cloud_->reserve(points.size());
    for (const auto& point : points) {
        cloud_->push_back(pcl::PointXYZ(point.x, point.y, point.z));
    }

    summaryLabel_->setText(QString("PCL 点云摘要: %1 个点").arg(cloud_->size()));
    // 统计点云包围盒，帮助快速判断重建尺度是否合理。

    if (!cloud_->empty()) {
        float minX = cloud_->front().x;
        float maxX = cloud_->front().x;
        float minY = cloud_->front().y;
        float maxY = cloud_->front().y;
        float minZ = cloud_->front().z;
        float maxZ = cloud_->front().z;
        for (const auto& point : *cloud_) {
            minX = std::min(minX, point.x);
            maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y);
            maxY = std::max(maxY, point.y);
            minZ = std::min(minZ, point.z);
            maxZ = std::max(maxZ, point.z);
        }
        boundsLabel_->setText(QString("边界框: X[%1, %2]  Y[%3, %4]  Z[%5, %6]")
                                  .arg(minX, 0, 'f', 4)
                                  .arg(maxX, 0, 'f', 4)
                                  .arg(minY, 0, 'f', 4)
                                  .arg(maxY, 0, 'f', 4)
                                  .arg(minZ, 0, 'f', 4)
                                  .arg(maxZ, 0, 'f', 4));
    } else {
        boundsLabel_->setText("边界框: -");
    }

    sampleTable_->setRowCount(std::min<int>(static_cast<int>(cloud_->size()), 20));
    for (int row = 0; row < sampleTable_->rowCount(); ++row) {
        const auto& point = cloud_->at(static_cast<std::size_t>(row));
        sampleTable_->setItem(row, 0, new QTableWidgetItem(QString::number(point.x, 'f', 4)));
        sampleTable_->setItem(row, 1, new QTableWidgetItem(QString::number(point.y, 'f', 4)));
        sampleTable_->setItem(row, 2, new QTableWidgetItem(QString::number(point.z, 'f', 4)));
    }
}

void PointcloudSummaryWidget::clearView() {
    cloud_->clear();
    summaryLabel_->setText("暂无点云结果");
    boundsLabel_->setText("边界框: -");
    sampleTable_->setRowCount(0);
}

}  // namespace htmsr::visualization
