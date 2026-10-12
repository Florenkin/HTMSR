#pragma once

#include "core/PointCloudProcessingService.h"
#include <QPolygon>
#include <QString>
#include <QWidget>
#include <memory>

namespace htmsr::app
{

class PointCloudViewWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit PointCloudViewWidget(QWidget* parent = nullptr);
    ~PointCloudViewWidget() override;
    const CloudPoints& currentPoints() const;
    std::size_t selectedPointCount() const;
    void selectIndices(const std::vector<std::size_t>& indices);
    bool isProcessing() const;
public slots:
    void setPoints(const CloudPoints& points);
    void clear();
signals:
    void processingFinished();

private:
    void buildToolbar();
    void addPointCloud(CloudPoints points, const QString& name, const QString& source);
    void loadPointCloud();
    void exportPointCloud();
    void rebuildPreviewList();
    void updateDetail(bool resetCamera = false);
    void editSelection(int operation);
    void processCloud(const QString& operation);
    void mergeClouds();
    void exportErrors();
    void showErrorAnalysis();
    void finishProcessing();
    void selectPolygon(const QPolygon& polygon, Qt::KeyboardModifiers modifiers);
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace htmsr::app
