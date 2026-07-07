// 文件说明：
// 定义项目中通用的二维、三维几何基础类型。

#pragma once

#include <cmath>
#include <vector>

namespace htmsr::data_model {

struct Point2D {
    float x = 0.f;
    float y = 0.f;

    Point2D() = default;
    Point2D(float x_value, float y_value) : x(x_value), y(y_value) {}

    [[nodiscard]] float vectorNorm() const {
        return std::sqrt(x * x + y * y);
    }
};

struct Point3D {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;

    Point3D() = default;
    Point3D(float x_value, float y_value, float z_value) : x(x_value), y(y_value), z(z_value) {}

    [[nodiscard]] float vectorNorm() const {
        return std::sqrt(x * x + y * y + z * z);
    }
};

using Point2DList = std::vector<Point2D>;
using Point3DList = std::vector<Point3D>;

}  // namespace htmsr::data_model
