#include "core/PointCloudService.h"

#include "core/FileSystemUtils.h"
#include "core/Logger.h"

#include <pcl/io/pcd_io.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <cstdint>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace htmsr {

std::vector<Eigen::Vector3d> PointCloudService::load(const std::string& filename) const
{
    std::string extension = std::filesystem::path(filename).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    std::vector<Eigen::Vector3d> points;
    if (extension == ".pcd") {
        pcl::PointCloud<pcl::PointXYZ> cloud;
        if (pcl::io::loadPCDFile(filename, cloud) != 0) {
            throw std::runtime_error("Failed to read point cloud PCD: " + filename);
        }
        points.reserve(cloud.size());
        for (const auto& point : cloud.points) {
            if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z)) {
                points.emplace_back(point.x, point.y, point.z);
            }
        }
    } else if (extension == ".txt" || extension == ".xyz") {
        std::ifstream input(filename);
        if (!input) {
            throw std::runtime_error("Failed to read point cloud text file: " + filename);
        }

        std::string line;
        int lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            const auto commentPosition = line.find('#');
            if (commentPosition != std::string::npos) {
                line.erase(commentPosition);
            }
            std::replace(line.begin(), line.end(), ',', ' ');
            std::istringstream stream(line);
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            if (!(stream >> x >> y >> z)) {
                stream.clear();
                stream.str(line);
                std::string remaining;
                if (stream >> remaining) {
                    throw std::runtime_error(
                        "Invalid point cloud data at line " + std::to_string(lineNumber) + ": " + filename);
                }
                continue;
            }
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
                points.emplace_back(x, y, z);
            }
        }
    } else {
        throw std::runtime_error("Unsupported point cloud format: " + extension);
    }

    if (points.empty()) {
        throw std::runtime_error("Point cloud file contains no valid XYZ points: " + filename);
    }
    Logger::instance().info("PointCloud", "Point cloud loaded: " + filename + ", points=" + std::to_string(points.size()));
    return points;
}

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
