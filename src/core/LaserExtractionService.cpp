#include "core/LaserExtractionService.h"

#include "core/Logger.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace htmsr {
namespace {

int channelIndex(LaserColor color)
{
    // OpenCV 彩色图像默认通道顺序为 BGR。
    switch (color) {
    case LaserColor::Blue:
        return 0;
    case LaserColor::Green:
        return 1;
    case LaserColor::Red:
        return 2;
    case LaserColor::Gray:
        return -1;
    }
    return 0;
}

cv::Mat toLaserGray(const cv::Mat& image, LaserColor color)
{
    // 根据激光颜色选择对应通道；灰度模式下直接转换为单通道图像。
    if (image.channels() == 1 || color == LaserColor::Gray) {
        cv::Mat gray;
        if (image.channels() == 1) {
            gray = image.clone();
        } else {
            cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        }
        return gray;
    }

    std::vector<cv::Mat> channels;
    cv::split(image, channels);
    return channels[channelIndex(color)].clone();
}

cv::Mat thresholdLaser(const cv::Mat& image, LaserColor color, double threshold)
{
    // 先按颜色通道提取激光灰度图，再通过阈值分割获得亮条纹区域。
    cv::Mat gray = toLaserGray(image, color);
    cv::Mat binary;
    cv::threshold(gray, binary, threshold, 255.0, cv::THRESH_BINARY);
    cv::morphologyEx(binary, binary, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
    return binary;
}

std::vector<cv::Rect> connectedRanges(const cv::Mat& binary, double minArea, int border)
{
    // 根据二值图中的连通区域估计激光条纹所在范围，减少后续逐像素计算量。
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<cv::Rect> ranges;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < minArea) {
            continue;
        }

        cv::Rect rect = cv::boundingRect(contour);
        rect.x = std::max(0, rect.x - border);
        rect.y = std::max(0, rect.y - border);
        rect.width = std::min(binary.cols - rect.x, rect.width + 2 * border);
        rect.height = std::min(binary.rows - rect.y, rect.height + 2 * border);
        if (rect.area() > 0) {
            ranges.push_back(rect);
        }
    }

    std::sort(ranges.begin(), ranges.end(), [](const cv::Rect& lhs, const cv::Rect& rhs) {
        return lhs.y == rhs.y ? lhs.x < rhs.x : lhs.y < rhs.y;
    });
    return ranges;
}

cv::Mat makePreview(const cv::Mat& image)
{
    // 预览图统一使用 BGR 彩色图，便于在上面绘制红色中心线点。
    cv::Mat preview;
    if (image.channels() == 1) {
        cv::cvtColor(image, preview, cv::COLOR_GRAY2BGR);
    } else {
        preview = image.clone();
    }
    return preview;
}

void trimEndpoints(std::vector<Eigen::Vector2d>& points, const LaserExtractionConfig& config)
{
    // 去除首尾若干点，减少激光条纹端点区域的检测波动。
    if (!config.removeEndPoints || config.removeEndPointCount <= 0) {
        return;
    }

    const auto removeCount = static_cast<size_t>(config.removeEndPointCount);
    if (points.size() <= removeCount * 2) {
        return;
    }
    points.erase(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(removeCount));
    points.erase(points.end() - static_cast<std::ptrdiff_t>(removeCount), points.end());
}

} // namespace

LaserExtractionResult LaserExtractionService::extract(const cv::Mat& image, const cv::Rect& roi, const LaserExtractionConfig& config) const
{
    if (image.empty()) {
        throw std::runtime_error("Cannot extract laser centerline from an empty image.");
    }

    // 用户输入的 ROI 可能超过图像边界，先裁剪到合法范围。
    const cv::Rect safeRoi = clampRoi(image, roi);
    if (safeRoi.empty()) {
        throw std::runtime_error("Laser ROI is outside image bounds.");
    }

    // 根据参数选择灰度重心法或 Steger 法。
    if (config.mode == LaserExtractionMode::Steger) {
        return extractSteger(image, safeRoi, config);
    }
    return extractGrayCentroid(image, safeRoi, config);
}

LaserExtractionResult LaserExtractionService::extractGrayCentroid(
    const cv::Mat& image,
    const cv::Rect& roi,
    const LaserExtractionConfig& config) const
{
    // 灰度重心法：在每一行内按灰度值加权求激光中心位置。
    const cv::Mat roiImage = image(roi).clone();
    cv::Mat gray = toLaserGray(roiImage, config.laserColor);
    cv::Mat binary;
    cv::threshold(gray, binary, config.grayThreshold, 255, cv::THRESH_BINARY);
    cv::morphologyEx(binary, binary, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));

    // 二值图用于限制有效区域，避免背景噪声参与中心计算。
    cv::Mat filtered;
    gray.copyTo(filtered, binary);

    auto ranges = connectedRanges(binary, 30.0, 2);
    if (ranges.empty()) {
        ranges.push_back(cv::Rect(0, 0, filtered.cols, filtered.rows));
    }

    LaserExtractionResult result;
    result.preview = makePreview(image);

    for (const auto& range : ranges) {
        for (int y = range.y; y < range.y + range.height; ++y) {
            // 对当前行的亮像素做加权平均，得到亚像素级 x 坐标。
            double weighted = 0.0;
            double total = 0.0;

            for (int x = range.x; x < range.x + range.width; ++x) {
                const auto value = static_cast<double>(filtered.at<uchar>(y, x));
                if (value <= config.minGray) {
                    continue;
                }

                total += value;
                weighted += value * static_cast<double>(x);
            }

            if (total < 200.0) {
                continue;
            }

            const double centerX = weighted / total + roi.x;
            const double centerY = static_cast<double>(y + roi.y);
            result.points.emplace_back(centerX, centerY);
            cv::circle(result.preview, cv::Point2d(centerX, centerY), 1, cv::Scalar(0, 0, 255), -1);
        }
    }

    trimEndpoints(result.points, config);
    return result;
}

LaserExtractionResult LaserExtractionService::extractSteger(
    const cv::Mat& image,
    const cv::Rect& roi,
    const LaserExtractionConfig& config) const
{
    // Steger 法：利用高斯导数和 Hessian 矩阵提取条纹亚像素中心。
    const cv::Mat roiImage = image(roi).clone();
    cv::Mat gray = toLaserGray(roiImage, config.laserColor);
    cv::Mat binary = thresholdLaser(roiImage, config.laserColor, config.binaryThreshold);
    const auto ranges = connectedRanges(binary, 500.0, 10);

    LaserExtractionResult result;
    result.preview = makePreview(image);

    if (ranges.empty()) {
        return result;
    }

    // 根据线宽估计高斯核尺度，参考公式 sigma = stripeWidth / sqrt(3)。
    const double sigma = std::max(0.5, config.stripeWidth / std::sqrt(3.0));
    const int radius = std::max(1, static_cast<int>(std::round(3.0 * sigma)));
    const int kernelSize = 2 * radius + 1;

    cv::Mat X(kernelSize, kernelSize, CV_64F);
    cv::Mat Y(kernelSize, kernelSize, CV_64F);
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            X.at<double>(y + radius, x + radius) = static_cast<double>(x);
            Y.at<double>(y + radius, x + radius) = static_cast<double>(y);
        }
    }

    // 手动构造一阶和二阶高斯导数核，便于后续卷积求 Hessian。
    cv::Mat expTerm;
    cv::exp(-(X.mul(X) + Y.mul(Y)) / (2.0 * sigma * sigma), expTerm);
    cv::Mat dGx = (1.0 / (2.0 * CV_PI * std::pow(sigma, 4))) * (-X).mul(expTerm);
    cv::Mat dGy = dGx.t();
    cv::Mat dGxx = (1.0 / (2.0 * CV_PI * std::pow(sigma, 4))) * ((X.mul(X) / (sigma * sigma)) - 1.0).mul(expTerm);
    cv::Mat dGxy = (1.0 / (2.0 * CV_PI * std::pow(sigma, 6))) * X.mul(Y).mul(expTerm);
    cv::Mat dGyy = dGxx.t();
    cv::flip(dGx, dGx, -1);
    cv::flip(dGy, dGy, -1);
    cv::flip(dGxx, dGxx, -1);
    cv::flip(dGxy, dGxy, -1);
    cv::flip(dGyy, dGyy, -1);

    cv::Mat visited = cv::Mat::zeros(gray.size(), CV_8UC1);
    for (const auto& range : ranges) {
        // 每个连通区域单独计算导数，降低无效背景区域的计算量。
        cv::Mat patch = gray(range).clone();
        patch.convertTo(patch, CV_64F);

        cv::Mat dx, dy, dxx, dxy, dyy;
        cv::filter2D(patch, dx, CV_64F, dGx, cv::Point(-1, -1), 0.0, cv::BORDER_CONSTANT);
        cv::filter2D(patch, dy, CV_64F, dGy, cv::Point(-1, -1), 0.0, cv::BORDER_CONSTANT);
        cv::filter2D(patch, dxx, CV_64F, dGxx, cv::Point(-1, -1), 0.0, cv::BORDER_CONSTANT);
        cv::filter2D(patch, dxy, CV_64F, dGxy, cv::Point(-1, -1), 0.0, cv::BORDER_CONSTANT);
        cv::filter2D(patch, dyy, CV_64F, dGyy, cv::Point(-1, -1), 0.0, cv::BORDER_CONSTANT);

        std::vector<cv::Point2d> rowCandidates;
        std::vector<cv::Point> pixelCandidates;
        for (int y = 0; y < patch.rows; ++y) {
            for (int x = 0; x < patch.cols; ++x) {
                const int imageX = x + range.x;
                const int imageY = y + range.y;
                if (patch.at<double>(y, x) <= config.selectionThreshold ||
                    visited.at<uchar>(imageY, imageX) > 0 ||
                    binary.at<uchar>(imageY, imageX) == 0) {
                    continue;
                }

                const double a = dxx.at<double>(y, x);
                const double b = dxy.at<double>(y, x);
                const double c = dyy.at<double>(y, x);
                // 根据 Hessian 特征方向估计激光条纹法线方向。
                const double trace = a + c;
                const double diff = a - c;
                const double root = std::sqrt(diff * diff + 4.0 * b * b);
                const double lambda1 = 0.5 * (trace + root);
                const double lambda2 = 0.5 * (trace - root);

                double nx = 2.0 * b;
                double ny = c - a + root;
                if (std::abs(lambda1) < std::abs(lambda2)) {
                    nx = -ny;
                    ny = 2.0 * b;
                }

                const double mag = std::sqrt(nx * nx + ny * ny);
                if (mag < 1e-9) {
                    continue;
                }
                nx /= mag;
                ny /= mag;

                // 沿法线方向求亚像素偏移，偏移过大说明该点不稳定。
                const double denominator = a * nx * nx + 2.0 * b * nx * ny + c * ny * ny;
                if (std::abs(denominator) < 1e-9) {
                    continue;
                }

                const double offset = -(dx.at<double>(y, x) * nx + dy.at<double>(y, x) * ny) / denominator;
                const double px = offset * nx;
                const double py = offset * ny;
                if (std::abs(px) <= 0.5 && std::abs(py) <= 0.5) {
                    rowCandidates.emplace_back(imageX + px, imageY + py);
                    pixelCandidates.emplace_back(imageX, imageY);
                    visited.at<uchar>(imageY, imageX) = 255;
                }
            }
        }

        if (config.filterStegerPoints) {
            // 同一行可能产生多个候选点，取中间位置作为该行最终中心点。
            for (size_t i = 0; i < rowCandidates.size();) {
                size_t j = i + 1;
                while (j < rowCandidates.size() && pixelCandidates[j].y == pixelCandidates[i].y) {
                    ++j;
                }
                const size_t middle = i + (j - i) / 2;
                result.points.emplace_back(rowCandidates[middle].x + roi.x, rowCandidates[middle].y + roi.y);
                i = j;
            }
        } else {
            for (const auto& point : rowCandidates) {
                result.points.emplace_back(point.x + roi.x, point.y + roi.y);
            }
        }
    }

    for (const auto& point : result.points) {
        cv::circle(result.preview, cv::Point2d(point.x(), point.y()), 1, cv::Scalar(0, 0, 255), -1);
    }

    trimEndpoints(result.points, config);
    return result;
}

cv::Rect LaserExtractionService::clampRoi(const cv::Mat& image, const cv::Rect& roi) const
{
    const cv::Rect bounds(0, 0, image.cols, image.rows);
    return roi & bounds;
}

} // namespace htmsr
