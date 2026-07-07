// 文件说明：
// 定义兼容 OpenCorr 的 POI 数据结构，供导出和后续分析使用。

#pragma once

#include <algorithm>
#include <array>

#include "data_model/geometry_types.h"

namespace htmsr::data_model {

union DeformationVector2D {
    struct {
        float u, ux, uy, uxx, uxy, uyy;
        float v, vx, vy, vxx, vxy, vyy;
    };
    float p[12];
};

union StrainVector2D {
    struct {
        float exx, eyy, exy;
    };
    float e[3];
};

union Result2D {
    struct {
        float u0, v0, zncc, iteration, convergence, feature;
    };
    float r[6];
};

union Result2DS {
    struct {
        float r1r2_zncc, r1t1_zncc, r1t2_zncc, r2_x, r2_y, t1_x, t1_y, t2_x, t2_y;
    };
    float r[9];
};

union DeformationVector3D {
    struct {
        float u, ux, uy, uz;
        float v, vx, vy, vz;
        float w, wx, wy, wz;
    };
    float p[12];
};

union DisplacementVector3D {
    struct {
        float u, v, w;
    };
    float p[3];
};

union StrainVector3D {
    struct {
        float exx, eyy, ezz, exy, eyz, ezx;
    };
    float e[6];
};

union Result3D {
    struct {
        float u0, v0, w0, zncc, iteration, convergence, feature;
    };
    float r[7];
};

class POI2D : public Point2D {
public:
    DeformationVector2D deformation{};
    Result2D result{};
    StrainVector2D strain{};
    Point2D subset_radius{};

    POI2D() { clear(); }
    POI2D(float x_value, float y_value) : Point2D(x_value, y_value) { clear(); }
    explicit POI2D(const Point2D& point) : Point2D(point) { clear(); }

    void clear() {
        std::fill(std::begin(deformation.p), std::end(deformation.p), 0.f);
        std::fill(std::begin(result.r), std::end(result.r), 0.f);
        std::fill(std::begin(strain.e), std::end(strain.e), 0.f);
        subset_radius = {};
    }
};

class POI2DS : public Point2D {
public:
    DisplacementVector3D deformation{};
    Result2DS result{};
    Point3D ref_coor{};
    Point3D tar_coor{};
    StrainVector3D strain{};
    Point2D subset_radius{};

    POI2DS() { clear(); }
    POI2DS(float x_value, float y_value) : Point2D(x_value, y_value) { clear(); }
    explicit POI2DS(const Point2D& point) : Point2D(point) { clear(); }

    void clear() {
        std::fill(std::begin(deformation.p), std::end(deformation.p), 0.f);
        std::fill(std::begin(result.r), std::end(result.r), 0.f);
        std::fill(std::begin(strain.e), std::end(strain.e), 0.f);
        ref_coor = {};
        tar_coor = {};
        subset_radius = {};
    }
};

class POI3D : public Point3D {
public:
    DeformationVector3D deformation{};
    Result3D result{};
    StrainVector3D strain{};
    Point3D subset_radius{};

    POI3D() { clear(); }
    POI3D(float x_value, float y_value, float z_value) : Point3D(x_value, y_value, z_value) { clear(); }
    explicit POI3D(const Point3D& point) : Point3D(point) { clear(); }

    void clear() {
        std::fill(std::begin(deformation.p), std::end(deformation.p), 0.f);
        std::fill(std::begin(result.r), std::end(result.r), 0.f);
        std::fill(std::begin(strain.e), std::end(strain.e), 0.f);
        subset_radius = {};
    }
};

}  // namespace htmsr::data_model
