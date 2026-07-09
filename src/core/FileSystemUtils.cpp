#include "core/FileSystemUtils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace htmsr {
namespace {

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isImageExtension(const std::filesystem::path& path)
{
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

    std::vector<std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && isImageExtension(entry.path())) {
            files.push_back(fs::absolute(entry.path()).string());
        }
    }

    std::sort(files.begin(), files.end());

    if (range.begin == -1 && range.end == -1) {
        return files;
    }

    if (files.empty()) {
        return files;
    }

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
    return fs::exists(parent) || fs::create_directories(parent);
}

} // namespace htmsr
