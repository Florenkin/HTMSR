#pragma once

#include "core/Types.h"

#include <Eigen/Core>

#include <string>
#include <vector>

namespace htmsr {

class PointCloudService {
public:
    // 从现有 TXT（每行 x y z）或 PCD 文件读取点云。
    std::vector<Eigen::Vector3d> load(const std::string& filename) const;

    /*
        函数功能：将逐帧重建得到的三维点集合并为一个点云
        输入：
            frames：逐帧重建结果集合
        输出：
            返回值：合并后的三维点集合
    */
    std::vector<Eigen::Vector3d> mergeFrames(const std::vector<FrameReconstructionResult>& frames) const;

    /*
        函数功能：将三维点云保存为 txt 文本文件
        输入：
            filename：输出 txt 文件路径
            points：待保存的三维点集合
        输出：
            无（函数执行后会在磁盘上生成或覆盖对应的 txt 文件）
    */
    void saveTxt(const std::string& filename, const std::vector<Eigen::Vector3d>& points, bool logSave = true) const;

    /*
        函数功能：将三维点云保存为标准二进制 PCD 文件
        输入：
            filename：输出 pcd 文件路径
            points：待保存的三维点集合
        输出：
            无（函数执行后会在磁盘上生成或覆盖对应的 pcd 文件）
    */
    void savePcd(const std::string& filename, const std::vector<Eigen::Vector3d>& points, bool logSave = true) const;
};

} // namespace htmsr
