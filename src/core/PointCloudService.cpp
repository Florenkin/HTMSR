#include "core/PointCloudService.h"

#include "core/FileSystemUtils.h"
#include "core/Logger.h"

#include <pcl/io/pcd_io.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace htmsr {

std::vector<Eigen::Vector3d> PointCloudService::mergeFrames(const std::vector<FrameReconstructionResult>& frames) const
{
    // 预先统计总点数，避免合并过程中频繁扩容。
    size_t count = 0;
    for (const auto& frame : frames) {
        count += frame.points.size();
    }

    // 将每一帧的点云顺序追加到同一个数组中。
    std::vector<Eigen::Vector3d> merged;
    merged.reserve(count);
    for (const auto& frame : frames) {
        merged.insert(merged.end(), frame.points.begin(), frame.points.end());
    }
    return merged;
}

void PointCloudService::saveTxt(const std::string& filename, const std::vector<Eigen::Vector3d>& points) const
{
    // TXT 格式按每行 x y z 保存，便于调试和其他软件快速读取。
    ensureParentDirectory(filename);
    std::ofstream out(filename, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Failed to write point cloud txt: " + filename);
    }

    for (const auto& point : points) {
        out << point.x() << ' ' << point.y() << ' ' << point.z() << '\n';
    }

    Logger::instance().info("PointCloud", "TXT point cloud saved: " + filename);
}

void PointCloudService::savePcd(const std::string& filename, const std::vector<Eigen::Vector3d>& points) const
{
    ensureParentDirectory(filename);

    // 将 Eigen 点集合转换为 PCL 点云对象，再保存为二进制 PCD。
    pcl::PointCloud<pcl::PointXYZ> cloud;
    cloud.height = 1;
    cloud.width = static_cast<std::uint32_t>(points.size());
    cloud.is_dense = true;
    cloud.points.reserve(points.size());

    for (const auto& point : points) {
        cloud.points.emplace_back(
            static_cast<float>(point.x()),
            static_cast<float>(point.y()),
            static_cast<float>(point.z()));
    }

    if (pcl::io::savePCDFileBinary(filename, cloud) != 0) {
        throw std::runtime_error("Failed to write point cloud pcd: " + filename);
    }

    Logger::instance().info("PointCloud", "PCD point cloud saved: " + filename);
}

} // namespace htmsr
