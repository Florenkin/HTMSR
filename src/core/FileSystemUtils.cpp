#include "core/FileSystemUtils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace htmsr {
namespace {

std::string toLower(std::string value)
{
    // 扩展名比较统一转为小写，避免 Windows 下大小写混用导致漏读。
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isImageExtension(const std::filesystem::path& path)
{
    // 当前离线流程支持常见相机图像格式。
    const auto ext = toLower(path.extension().string());
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp" || ext == ".tif" || ext == ".tiff";
}

} // namespace

std::vector<std::string> listImageFiles(const std::string& directory, const ImageRange& range)
{
    namespace fs = std::filesystem;

    if (!fs::is_directory(directory)) {
        throw std::runtime_error("Image directory does not exist: " + directory);
    }

    // 递归扫描目录，收集所有可用图像路径。
    std::vector<std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && isImageExtension(entry.path())) {
            files.push_back(fs::absolute(entry.path()).string());
        }
    }

    // 按文件名排序，保证左右目录在命名一致时能够稳定配对。
    std::sort(files.begin(), files.end());

    // (-1, -1) 表示保留全部图像。
    if (range.begin == -1 && range.end == -1) {
        return files;
    }

    if (files.empty()) {
        return files;
    }

    // 对索引范围做边界裁剪，避免用户输入越界导致崩溃。
    const int first = std::max(0, range.begin);
    const int last = std::min(range.end, static_cast<int>(files.size()) - 1);
    if (first > last) {
        return {};
    }

    return std::vector<std::string>(files.begin() + first, files.begin() + last + 1);
}

bool ensureParentDirectory(const std::string& filePath)
{
    namespace fs = std::filesystem;
    const fs::path path(filePath);
    const auto parent = path.parent_path();
    if (parent.empty()) {
        return true;
    }
    // 父目录不存在时自动创建，便于保存标定文件和点云文件。
    return fs::exists(parent) || fs::create_directories(parent);
}

} // namespace htmsr
