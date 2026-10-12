#pragma once

#include "core/Cancellation.h"
#include <Eigen/Core>
#include <vector>

namespace htmsr
{

using CloudPoints = std::vector<Eigen::Vector3d>;
enum class CloudFitKind
{
    Plane,
    Sphere,
    Cylinder,
    Quadric,
    Quartic
};

struct CloudErrorStatistics
{
    double mean = 0.0;
    double rmse = 0.0;
    double maximumAbsolute = 0.0;
};

struct CloudFitResult
{
    CloudFitKind kind = CloudFitKind::Plane;
    Eigen::Vector3d center = Eigen::Vector3d::Zero();
    Eigen::Vector3d direction = Eigen::Vector3d::UnitZ();
    double offset = 0.0;
    double radius = 0.0;
    double axisMinimum = 0.0;
    double axisMaximum = 0.0;
    double standardDiameter = 0.0;
    Eigen::Vector3d basisU = Eigen::Vector3d::UnitX();
    Eigen::Vector3d basisV = Eigen::Vector3d::UnitY();
    double surfaceScale = 1.0;
    std::vector<double> coefficients;
    std::vector<double> errors;
    CloudErrorStatistics statistics;
};

struct CloudDefect
{
    bool depression = false;
    std::vector<std::size_t> indices;
    double maximumDepth = 0.0;
    double length = 0.0;
    double width = 0.0;
    double projectedArea = 0.0;
};

// 算法只操作完整点云坐标，不依赖 Qt/VTK 或显示抽样。
class PointCloudProcessingService
{
public:
    CloudPoints filterFinite(const CloudPoints& points) const;
    CloudPoints voxelFilter(
        const CloudPoints& points, double size, CancellationToken cancellation = { }) const;
    CloudPoints statisticalFilter(const CloudPoints& points, int neighbors, double standardDeviation,
        CancellationToken cancellation = { }) const;
    CloudPoints passThrough(const CloudPoints& points, int axis, double minimum, double maximum) const;
    CloudFitResult fit(const CloudPoints& points, CloudFitKind kind, double standardDiameter = 0.0,
        CancellationToken cancellation = { }) const;
    std::vector<CloudFitResult> fitDoubleSphere(
        const CloudPoints& points, double standardDiameter = 0.0, CancellationToken cancellation = { }) const;
    std::vector<CloudDefect> measureDefects(const CloudPoints& points, const CloudFitResult& reference,
        double minimumDepth, double clusterDistance, int minimumPoints,
        CancellationToken cancellation = { }) const;
    double estimateSpacing(const CloudPoints& points, CancellationToken cancellation = { }) const;
    static double signedError(const Eigen::Vector3d& point, const CloudFitResult& reference);
    static double surfaceHeight(double u, double v, const CloudFitResult& reference);
};

} // namespace htmsr
