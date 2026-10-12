#include "core/PointCloudProcessingService.h"

#include <Eigen/Cholesky>
#include <Eigen/Eigenvalues>
#include <Eigen/Geometry>
#include <Eigen/QR>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <stdexcept>

namespace htmsr
{
namespace
{

    class KdTree
    {
        struct Node
        {
            std::size_t index;
            int axis;
            int left = -1;
            int right = -1;
        };
        const CloudPoints& points_;
        std::vector<Node> nodes_;
        std::vector<std::size_t> indices_;
        CancellationToken cancellation_;
        int build(std::size_t begin, std::size_t end, int depth)
        {
            if (begin == end)
                return -1;
            if ((nodes_.size() & 1023) == 0)
                cancellation_.check();
            const auto middle = begin + (end - begin) / 2;
            const int axis = depth % 3;
            std::nth_element(indices_.begin() + begin, indices_.begin() + middle, indices_.begin() + end,
                [&](auto a, auto b) { return points_[a][axis] < points_[b][axis]; });
            const int node = static_cast<int>(nodes_.size());
            nodes_.push_back({ indices_[middle], axis });
            const int left = build(begin, middle, depth + 1);
            const int right = build(middle + 1, end, depth + 1);
            nodes_[node].left = left;
            nodes_[node].right = right;
            return node;
        }
        void nearest(int node, std::size_t query, int count,
            std::priority_queue<std::pair<double, std::size_t>>& found) const
        {
            if (node < 0)
                return;
            const auto& n = nodes_[node];
            const double distance = (points_[n.index] - points_[query]).squaredNorm();
            if (n.index != query)
            {
                if (static_cast<int>(found.size()) < count)
                    found.emplace(distance, n.index);
                else if (distance < found.top().first)
                {
                    found.pop();
                    found.emplace(distance, n.index);
                }
            }
            const double delta = points_[query][n.axis] - points_[n.index][n.axis];
            nearest(delta < 0 ? n.left : n.right, query, count, found);
            if (static_cast<int>(found.size()) < count || delta * delta <= found.top().first)
                nearest(delta < 0 ? n.right : n.left, query, count, found);
        }
        void radius(
            int node, const Eigen::Vector3d& query, double squared, std::vector<std::size_t>& found) const
        {
            if (node < 0)
                return;
            const auto& n = nodes_[node];
            if ((points_[n.index] - query).squaredNorm() <= squared)
                found.push_back(n.index);
            const double delta = query[n.axis] - points_[n.index][n.axis];
            radius(delta < 0 ? n.left : n.right, query, squared, found);
            if (delta * delta <= squared)
                radius(delta < 0 ? n.right : n.left, query, squared, found);
        }

    public:
        explicit KdTree(const CloudPoints& points, CancellationToken cancellation = { })
            : points_(points)
            , cancellation_(cancellation)
        {
            indices_.resize(points.size());
            std::iota(indices_.begin(), indices_.end(), 0);
            nodes_.reserve(points.size());
            build(0, points.size(), 0);
        }
        std::vector<double> distances(std::size_t query, int count) const
        {
            std::priority_queue<std::pair<double, std::size_t>> found;
            nearest(0, query, count, found);
            std::vector<double> result;
            while (!found.empty())
            {
                result.push_back(std::sqrt(found.top().first));
                found.pop();
            }
            return result;
        }
        std::vector<std::size_t> within(const Eigen::Vector3d& query, double distance) const
        {
            std::vector<std::size_t> result;
            radius(0, query, distance * distance, result);
            return result;
        }
    };

    Eigen::Vector3d centroid(const CloudPoints& points)
    {
        Eigen::Vector3d center = Eigen::Vector3d::Zero();
        for (const auto& point : points)
            center += point / static_cast<double>(points.size());
        return center;
    }

    Eigen::Matrix3d covariance(const CloudPoints& points, const Eigen::Vector3d& center)
    {
        Eigen::Matrix3d matrix = Eigen::Matrix3d::Zero();
        for (const auto& point : points)
        {
            const Eigen::Vector3d delta = point - center;
            matrix += delta * delta.transpose();
        }
        return matrix / static_cast<double>(points.size());
    }

    void orient(Eigen::Vector3d& direction)
    {
        Eigen::Index dominant;
        direction.cwiseAbs().maxCoeff(&dominant);
        if (direction[dominant] < 0.0)
            direction = -direction;
    }

    double signedDistance(const Eigen::Vector3d& point, const CloudFitResult& fit)
    {
        if (fit.kind == CloudFitKind::Plane)
            return fit.direction.dot(point - fit.center);
        const Eigen::Vector3d delta = point - fit.center;
        if (fit.kind == CloudFitKind::Quadric || fit.kind == CloudFitKind::Quartic)
            return delta.dot(fit.direction)
                - PointCloudProcessingService::surfaceHeight(
                    delta.dot(fit.basisU), delta.dot(fit.basisV), fit);
        const double radius = fit.standardDiameter > 0 ? fit.standardDiameter * 0.5 : fit.radius;
        if (fit.kind == CloudFitKind::Sphere)
            return delta.norm() - radius;
        return (delta - delta.dot(fit.direction) * fit.direction).norm() - radius;
    }

    void summarize(CloudFitResult& fit, const CloudPoints& points, CancellationToken cancellation)
    {
        fit.errors.clear();
        fit.errors.reserve(points.size());
        fit.statistics = { };
        fit.axisMinimum = std::numeric_limits<double>::max();
        fit.axisMaximum = std::numeric_limits<double>::lowest();
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            if ((i & 1023) == 0)
                cancellation.check();
            const double error = signedDistance(points[i], fit);
            fit.errors.push_back(error);
            fit.statistics.mean += error / points.size();
            fit.statistics.rmse += error * error / points.size();
            fit.statistics.maximumAbsolute = std::max(fit.statistics.maximumAbsolute, std::abs(error));
            const double axis = (points[i] - fit.center).dot(fit.direction);
            fit.axisMinimum = std::min(fit.axisMinimum, axis);
            fit.axisMaximum = std::max(fit.axisMaximum, axis);
        }
        fit.statistics.rmse = std::sqrt(fit.statistics.rmse);
    }

    CloudFitResult leastSquares(const CloudPoints& points, CloudFitKind kind)
    {
        CloudFitResult fit;
        fit.kind = kind;
        fit.center = centroid(points);
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(covariance(points, fit.center));
        if (solver.info() != Eigen::Success || solver.eigenvalues()[1] <= 1e-12)
            throw std::runtime_error("点云退化，无法确定有效曲面。请检查是否只有重复点或共线点。");
        if (kind == CloudFitKind::Plane || kind == CloudFitKind::Quadric || kind == CloudFitKind::Quartic)
        {
            fit.direction = solver.eigenvectors().col(0);
            orient(fit.direction);
            fit.offset = -fit.direction.dot(fit.center);
            if (kind != CloudFitKind::Plane)
            {
                fit.basisU = fit.direction.unitOrthogonal();
                fit.basisV = fit.direction.cross(fit.basisU);
                const int degree = kind == CloudFitKind::Quadric ? 2 : 4;
                const int columns = (degree + 1) * (degree + 2) / 2;
                fit.surfaceScale = std::sqrt(solver.eigenvalues().sum());
                Eigen::MatrixXd design(points.size(), columns);
                Eigen::VectorXd values(points.size());
                for (std::size_t row = 0; row < points.size(); ++row)
                {
                    const Eigen::Vector3d delta = points[row] - fit.center;
                    const double u = delta.dot(fit.basisU) / fit.surfaceScale,
                                 v = delta.dot(fit.basisV) / fit.surfaceScale;
                    int column = 0;
                    for (int total = 0; total <= degree; ++total)
                        for (int x = 0; x <= total; ++x)
                            design(row, column++) = std::pow(u, x) * std::pow(v, total - x);
                    values[row] = delta.dot(fit.direction);
                }
                auto qr = design.colPivHouseholderQr();
                if (qr.rank() < columns)
                    throw std::runtime_error("曲面拟合的采样分布不足，无法确定所有系数。");
                const Eigen::VectorXd coefficients = qr.solve(values);
                if (!coefficients.allFinite())
                    throw std::runtime_error("曲面拟合得到无效系数。");
                fit.coefficients.assign(coefficients.data(), coefficients.data() + coefficients.size());
            }
        }
        else if (kind == CloudFitKind::Sphere)
        {
            Eigen::MatrixXd design(points.size(), 4);
            Eigen::VectorXd values(points.size());
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                const Eigen::Vector3d delta = points[i] - fit.center;
                design.row(i) << 2 * delta.x(), 2 * delta.y(), 2 * delta.z(), 1;
                values[i] = delta.squaredNorm();
            }
            auto qr = design.colPivHouseholderQr();
            if (qr.rank() < 4)
                throw std::runtime_error("球拟合需要非共面的球面点。");
            const Eigen::Vector4d solution = qr.solve(values);
            const double squared = solution.head<3>().squaredNorm() + solution[3];
            if (!solution.allFinite() || squared <= 0)
                throw std::runtime_error("点云不能确定有效球面。");
            fit.center += solution.head<3>();
            fit.radius = std::sqrt(squared);
        }
        else
        {
            // 对三条主轴分别试拟合，避免短粗圆柱被误判为最长主轴。
            double best = std::numeric_limits<double>::max();
            const Eigen::Vector3d origin = fit.center;
            for (int candidate = 0; candidate < 3; ++candidate)
            {
                const Eigen::Vector3d axis = solver.eigenvectors().col(candidate);
                const Eigen::Vector3d u = axis.unitOrthogonal(), v = axis.cross(u);
                Eigen::MatrixXd design(points.size(), 3);
                Eigen::VectorXd values(points.size());
                for (std::size_t i = 0; i < points.size(); ++i)
                {
                    const Eigen::Vector3d delta = points[i] - origin;
                    const double x = delta.dot(u), y = delta.dot(v);
                    design.row(i) << 2 * x, 2 * y, 1;
                    values[i] = x * x + y * y;
                }
                auto qr = design.colPivHouseholderQr();
                if (qr.rank() < 3)
                    continue;
                const Eigen::Vector3d circle = qr.solve(values);
                const double squared = circle.head<2>().squaredNorm() + circle.z();
                if (!circle.allFinite() || squared <= 0)
                    continue;
                CloudFitResult trial;
                trial.kind = kind;
                trial.center = origin + u * circle.x() + v * circle.y();
                trial.direction = axis;
                trial.radius = std::sqrt(squared);
                double score = 0;
                for (const auto& point : points)
                {
                    const double error = signedDistance(point, trial);
                    score += error * error;
                }
                if (score < best)
                {
                    best = score;
                    fit = trial;
                }
            }
            if (!std::isfinite(best) || best == std::numeric_limits<double>::max())
                throw std::runtime_error("点云不能确定有效圆柱面。");
            orient(fit.direction);
        }
        return fit;
    }

    // 在局部坐标中迭代几何距离，圆柱轴使用正交切平面的两个增量。
    void refine(CloudFitResult& fit, const CloudPoints& points, CancellationToken cancellation)
    {
        if (fit.kind != CloudFitKind::Sphere && fit.kind != CloudFitKind::Cylinder)
            return;
        const int count = fit.kind == CloudFitKind::Sphere ? 4 : 5;
        for (int iteration = 0; iteration < 25; ++iteration)
        {
            cancellation.check();
            Eigen::MatrixXd normal = Eigen::MatrixXd::Zero(count, count);
            Eigen::VectorXd gradient = Eigen::VectorXd::Zero(count);
            const Eigen::Vector3d u = fit.direction.unitOrthogonal();
            const Eigen::Vector3d v = fit.direction.cross(u);
            for (const auto& point : points)
            {
                const Eigen::Vector3d delta = point - fit.center;
                Eigen::VectorXd jacobian(count);
                if (fit.kind == CloudFitKind::Sphere)
                {
                    const double length = delta.norm();
                    if (length < 1e-12)
                        continue;
                    jacobian.head<3>() = -delta / length;
                    jacobian[3] = -1;
                }
                else
                {
                    const double t = delta.dot(fit.direction);
                    const Eigen::Vector3d radial = delta - t * fit.direction;
                    const double length = radial.norm();
                    if (length < 1e-12)
                        continue;
                    const Eigen::Vector3d r = radial / length;
                    jacobian << -r.dot(u), -r.dot(v), -t * r.dot(u), -t * r.dot(v), -1;
                }
                const double residual = signedDistance(point, fit);
                normal += jacobian * jacobian.transpose();
                gradient += jacobian * residual;
            }
            normal.diagonal().array() += 1e-8;
            const Eigen::VectorXd step = normal.ldlt().solve(-gradient);
            if (!step.allFinite() || step.norm() < 1e-9)
                break;
            if (step.norm() > std::max(1.0, fit.radius) * 2)
                break;
            if (fit.kind == CloudFitKind::Sphere)
            {
                fit.center += step.head<3>();
                fit.radius += step[3];
            }
            else
            {
                fit.center += u * step[0] + v * step[1];
                fit.direction = (fit.direction + u * step[2] + v * step[3]).normalized();
                fit.radius += step[4];
            }
            if (fit.radius <= 0 || !fit.center.allFinite())
                throw std::runtime_error("曲面拟合未收敛，请重新选取有效表面。");
        }
        orient(fit.direction);
    }

} // namespace

CloudPoints PointCloudProcessingService::filterFinite(const CloudPoints& points) const
{
    CloudPoints output;
    output.reserve(points.size());
    for (const auto& point : points)
        if (point.allFinite())
            output.push_back(point);
    return output;
}

CloudPoints PointCloudProcessingService::voxelFilter(
    const CloudPoints& points, double size, CancellationToken cancellation) const
{
    if (!std::isfinite(size) || size <= 0)
        throw std::runtime_error("体素尺寸必须大于零。");
    struct Cell
    {
        Eigen::Vector3d sum = Eigen::Vector3d::Zero();
        std::size_t count = 0;
    };
    std::map<std::array<std::int64_t, 3>, Cell> cells;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        if ((i & 1023) == 0)
            cancellation.check();
        if (!points[i].allFinite())
            continue;
        std::array<std::int64_t, 3> key;
        for (int axis = 0; axis < 3; ++axis)
        {
            const double value = std::floor(points[i][axis] / size);
            if (value < -9e18 || value > 9e18)
                throw std::runtime_error("体素尺寸过小或坐标范围过大。");
            key[axis] = static_cast<std::int64_t>(value);
        }
        auto& cell = cells[key];
        cell.sum += points[i];
        ++cell.count;
    }
    CloudPoints output;
    output.reserve(cells.size());
    for (const auto& item : cells)
        output.push_back(item.second.sum / static_cast<double>(item.second.count));
    return output;
}

CloudPoints PointCloudProcessingService::statisticalFilter(
    const CloudPoints& source, int neighbors, double deviation, CancellationToken cancellation) const
{
    if (neighbors < 1 || !std::isfinite(deviation) || deviation <= 0)
        throw std::runtime_error("邻居数和标准差倍数必须大于零。");
    const auto points = filterFinite(source);
    if (points.size() < 3)
        throw std::runtime_error("统计滤波至少需要三个有效点。");
    neighbors = std::min<int>(neighbors, static_cast<int>(points.size() - 1));
    KdTree tree(points, cancellation);
    std::vector<double> means(points.size());
    double mean = 0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        if ((i & 255) == 0)
            cancellation.check();
        const auto distances = tree.distances(i, neighbors);
        means[i] = std::accumulate(distances.begin(), distances.end(), 0.0) / distances.size();
        mean += means[i] / points.size();
    }
    double variance = 0;
    for (auto value : means)
        variance += (value - mean) * (value - mean) / static_cast<double>(points.size() - 1);
    const double limit = mean + deviation * std::sqrt(variance);
    CloudPoints output;
    output.reserve(points.size());
    for (std::size_t i = 0; i < points.size(); ++i)
        if (means[i] <= limit)
            output.push_back(points[i]);
    return output;
}

CloudPoints PointCloudProcessingService::passThrough(
    const CloudPoints& points, int axis, double minimum, double maximum) const
{
    if (axis < 0 || axis > 2 || !std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
        throw std::runtime_error("直通滤波轴或上下限无效。");
    CloudPoints output;
    for (const auto& point : points)
        if (point.allFinite() && point[axis] >= minimum && point[axis] <= maximum)
            output.push_back(point);
    return output;
}

CloudFitResult PointCloudProcessingService::fit(const CloudPoints& source, CloudFitKind kind,
    double standardDiameter, CancellationToken cancellation) const
{
    cancellation.check();
    if (!std::isfinite(standardDiameter) || standardDiameter < 0)
        throw std::runtime_error("标准直径不能为负数。");
    const auto points = filterFinite(source);
    const std::size_t minimum = kind == CloudFitKind::Plane ? 3
        : kind == CloudFitKind::Sphere                      ? 4
        : kind == CloudFitKind::Quartic                     ? 15
                                                            : 6;
    if (points.size() < minimum)
        throw std::runtime_error("有效点不足，无法进行当前几何拟合。");
    // 拟合可使用均匀样本控制计算量；残差始终在完整输入上计算。
    CloudPoints sample;
    const std::size_t count = std::min<std::size_t>(points.size(), 20000);
    for (std::size_t i = 0; i < count; ++i)
        sample.push_back(points[i * points.size() / count]);
    auto result = leastSquares(sample, kind);
    // 确定性随机子集寻找低中位残差模型，避免少量远离群点拖偏平面或球面。
    if (kind == CloudFitKind::Plane || kind == CloudFitKind::Sphere)
    {
        const auto medianError = [&](const CloudFitResult& candidate)
        {
            std::vector<double> values;
            values.reserve(sample.size());
            for (const auto& point : sample)
                values.push_back(std::abs(signedDistance(point, candidate)));
            std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
            return values[values.size() / 2];
        };
        double best = medianError(result);
        std::mt19937 generator(12345);
        std::uniform_int_distribution<std::size_t> distribution(0, sample.size() - 1);
        for (int iteration = 0; iteration < 120 && best > 1e-10; ++iteration)
        {
            cancellation.check();
            CloudPoints subset;
            std::vector<std::size_t> indices;
            while (indices.size() < minimum)
            {
                const auto index = distribution(generator);
                if (std::find(indices.begin(), indices.end(), index) == indices.end())
                    indices.push_back(index);
            }
            for (auto index : indices)
                subset.push_back(sample[index]);
            try
            {
                auto candidate = leastSquares(subset, kind);
                const double score = medianError(candidate);
                if (score < best)
                {
                    best = score;
                    result = candidate;
                }
            }
            catch (const std::runtime_error&)
            {
            }
        }
    }
    else
        refine(result, sample, cancellation);
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        cancellation.check();
        std::vector<double> absolute;
        absolute.reserve(sample.size());
        for (const auto& point : sample)
            absolute.push_back(std::abs(signedDistance(point, result)));
        std::nth_element(absolute.begin(), absolute.begin() + absolute.size() / 2, absolute.end());
        const double limit = std::max(1e-8, absolute[absolute.size() / 2] * 3.0);
        CloudPoints inliers;
        for (const auto& point : sample)
            if (std::abs(signedDistance(point, result)) <= limit)
                inliers.push_back(point);
        if (inliers.size() < minimum)
            break;
        if (inliers.size() == sample.size())
        {
            refine(result, sample, cancellation);
            break;
        }
        auto refined = leastSquares(inliers, kind);
        refine(refined, inliers, cancellation);
        result = refined;
    }
    result.standardDiameter
        = kind == CloudFitKind::Sphere || kind == CloudFitKind::Cylinder ? standardDiameter : 0;
    summarize(result, points, cancellation);
    return result;
}

std::vector<CloudFitResult> PointCloudProcessingService::fitDoubleSphere(
    const CloudPoints& source, double standardDiameter, CancellationToken cancellation) const
{
    const auto points = filterFinite(source);
    if (points.size() < 8)
        throw std::runtime_error("双球拟合至少需要八个有效点。");
    const Eigen::Vector3d center = centroid(points);
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(covariance(points, center));
    const Eigen::Vector3d axis = solver.eigenvectors().col(2);
    std::vector<double> positions;
    for (const auto& point : points)
        positions.push_back((point - center).dot(axis));
    std::nth_element(positions.begin(), positions.begin() + positions.size() / 2, positions.end());
    CloudPoints first, second;
    for (const auto& point : points)
        ((point - center).dot(axis) <= positions[positions.size() / 2] ? first : second).push_back(point);
    return { fit(first, CloudFitKind::Sphere, standardDiameter, cancellation),
        fit(second, CloudFitKind::Sphere, standardDiameter, cancellation) };
}

double PointCloudProcessingService::estimateSpacing(
    const CloudPoints& source, CancellationToken cancellation) const
{
    const auto points = filterFinite(source);
    if (points.size() < 2)
        return 0;
    KdTree tree(points, cancellation);
    std::vector<double> values;
    const auto count = std::min<std::size_t>(points.size(), 2000);
    for (std::size_t i = 0; i < count; ++i)
    {
        if ((i & 255) == 0)
            cancellation.check();
        const auto distances = tree.distances(i * points.size() / count, 1);
        if (!distances.empty() && distances[0] > 0)
            values.push_back(distances[0]);
    }
    if (values.empty())
        return 0;
    std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
    return values[values.size() / 2];
}

std::vector<CloudDefect> PointCloudProcessingService::measureDefects(const CloudPoints& points,
    const CloudFitResult& reference, double depth, double clusterDistance, int minimumPoints,
    CancellationToken cancellation) const
{
    if (!std::isfinite(depth) || depth <= 0 || !std::isfinite(clusterDistance) || clusterDistance <= 0
        || minimumPoints < 1)
        throw std::runtime_error("缺陷深度、连通距离和最小点数必须大于零。");
    CloudPoints candidates;
    std::vector<std::size_t> original;
    std::vector<double> errors;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        if (!points[i].allFinite())
            continue;
        const double error = signedDistance(points[i], reference);
        if (std::abs(error) >= depth)
        {
            candidates.push_back(points[i]);
            original.push_back(i);
            errors.push_back(error);
        }
    }
    if (candidates.empty())
        return { };
    KdTree tree(candidates, cancellation);
    std::vector<bool> visited(candidates.size(), false);
    std::vector<CloudDefect> output;
    for (std::size_t start = 0; start < candidates.size(); ++start)
    {
        cancellation.check();
        if (visited[start])
            continue;
        CloudDefect defect;
        defect.depression = errors[start] < 0;
        std::vector<std::size_t> queue { start };
        visited[start] = true;
        for (std::size_t cursor = 0; cursor < queue.size(); ++cursor)
        {
            if ((cursor & 255) == 0)
                cancellation.check();
            const auto i = queue[cursor];
            defect.indices.push_back(original[i]);
            defect.maximumDepth = std::max(defect.maximumDepth, std::abs(errors[i]));
            for (auto next : tree.within(candidates[i], clusterDistance))
                if (!visited[next] && (errors[next] < 0) == defect.depression)
                {
                    visited[next] = true;
                    queue.push_back(next);
                }
        }
        if (defect.indices.size() < static_cast<std::size_t>(minimumPoints))
            continue;
        CloudPoints component;
        for (auto i : defect.indices)
            component.push_back(points[i]);
        const auto componentCenter = centroid(component);
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(covariance(component, componentCenter));
        const Eigen::Vector3d u = solver.eigenvectors().col(2), v = solver.eigenvectors().col(1);
        double minU = std::numeric_limits<double>::max(), maxU = -minU, minV = minU, maxV = -minU;
        for (const auto& point : component)
        {
            const Eigen::Vector3d delta = point - componentCenter;
            minU = std::min(minU, delta.dot(u));
            maxU = std::max(maxU, delta.dot(u));
            minV = std::min(minV, delta.dot(v));
            maxV = std::max(maxV, delta.dot(v));
        }
        defect.length = maxU - minU;
        defect.width = maxV - minV;
        defect.projectedArea = defect.length * defect.width; // 局部投影包围矩形面积，非真实表面积。
        output.push_back(std::move(defect));
    }
    return output;
}

double PointCloudProcessingService::signedError(const Eigen::Vector3d& point, const CloudFitResult& reference)
{
    return signedDistance(point, reference);
}

double PointCloudProcessingService::surfaceHeight(double u, double v, const CloudFitResult& reference)
{
    const int degree = reference.kind == CloudFitKind::Quadric ? 2 : 4;
    u /= reference.surfaceScale;
    v /= reference.surfaceScale;
    double height = 0;
    std::size_t column = 0;
    for (int total = 0; total <= degree; ++total)
        for (int x = 0; x <= total; ++x)
        {
            if (column >= reference.coefficients.size())
                return height;
            height += reference.coefficients[column++] * std::pow(u, x) * std::pow(v, total - x);
        }
    return height;
}

} // namespace htmsr
