#pragma once

#include "core/Types.h"

#include <Eigen/Core>

#include <string>
#include <vector>

namespace htmsr {

class PointCloudService {
public:
    std::vector<Eigen::Vector3d> mergeFrames(const std::vector<FrameReconstructionResult>& frames) const;
    void saveTxt(const std::string& filename, const std::vector<Eigen::Vector3d>& points) const;
    void savePcd(const std::string& filename, const std::vector<Eigen::Vector3d>& points) const;
};

} // namespace htmsr
