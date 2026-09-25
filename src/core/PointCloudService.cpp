#include "core/PointCloudService.h"

#include "core/FileSystemUtils.h"
#include "core/Logger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace htmsr {
namespace {

struct PcdField {
    std::string name;
    std::size_t size = 0;
    char type = '\0';
    std::size_t count = 1;
    std::size_t byteOffset = 0;
    std::size_t valueOffset = 0;
};

struct PcdHeader {
    std::vector<PcdField> fields;
    std::size_t width = 0;
    std::size_t height = 1;
    std::size_t points = 0;
    std::size_t pointSize = 0;
    std::size_t valueCount = 0;
    std::string data;
};

std::string uppercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

std::vector<std::string> splitValues(const std::string& value)
{
    std::istringstream stream(value);
    std::vector<std::string> result;
    std::string item;
    while (stream >> item) {
        result.push_back(item);
    }
    return result;
}

std::size_t parseSize(const std::string& value, const std::string& key, const std::string& filename)
{
    std::size_t consumed = 0;
    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(value, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid PCD " + key + " value in: " + filename);
    }
    if (consumed != value.size() || parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Invalid PCD " + key + " value in: " + filename);
    }
    return static_cast<std::size_t>(parsed);
}

PcdHeader readPcdHeader(std::istream& input, const std::string& filename)
{
    std::vector<std::string> names;
    std::vector<std::string> sizes;
    std::vector<std::string> types;
    std::vector<std::string> counts;
    PcdHeader header;
    std::string line;
    bool foundData = false;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        std::istringstream stream(line.substr(first));
        std::string key;
        stream >> key;
        key = uppercase(key);
        std::string value;
        std::getline(stream, value);
        const auto valueStart = value.find_first_not_of(" \t");
        value = valueStart == std::string::npos ? std::string{} : value.substr(valueStart);

        if (key == "FIELDS" || key == "FIELD") names = splitValues(value);
        else if (key == "SIZE") sizes = splitValues(value);
        else if (key == "TYPE") types = splitValues(value);
        else if (key == "COUNT") counts = splitValues(value);
        else if (key == "WIDTH") header.width = parseSize(value, key, filename);
        else if (key == "HEIGHT") header.height = parseSize(value, key, filename);
        else if (key == "POINTS") header.points = parseSize(value, key, filename);
        else if (key == "DATA") {
            header.data = value;
            std::transform(header.data.begin(), header.data.end(), header.data.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            foundData = true;
            break;
        }
    }

    if (!foundData || names.empty() || sizes.size() != names.size() || types.size() != names.size()) {
        throw std::runtime_error("Incomplete PCD header: " + filename);
    }
    if (counts.empty()) counts.assign(names.size(), "1");
    if (counts.size() != names.size()) {
        throw std::runtime_error("Invalid PCD COUNT field: " + filename);
    }
    if (header.points == 0) {
        if (header.width == 0 || header.height == 0 || header.width > std::numeric_limits<std::size_t>::max() / header.height) {
            throw std::runtime_error("Invalid PCD point count: " + filename);
        }
        header.points = header.width * header.height;
    }

    for (std::size_t index = 0; index < names.size(); ++index) {
        PcdField field;
        field.name = names[index];
        std::transform(field.name.begin(), field.name.end(), field.name.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        field.size = parseSize(sizes[index], "SIZE", filename);
        field.count = parseSize(counts[index], "COUNT", filename);
        field.type = types[index].empty() ? '\0' : static_cast<char>(std::toupper(static_cast<unsigned char>(types[index][0])));
        if (field.size == 0 || field.count == 0 || (field.type != 'F' && field.type != 'I' && field.type != 'U') ||
            field.count > std::numeric_limits<std::size_t>::max() / field.size ||
            header.pointSize > std::numeric_limits<std::size_t>::max() - field.size * field.count) {
            throw std::runtime_error("Unsupported PCD field definition: " + filename);
        }
        field.byteOffset = header.pointSize;
        field.valueOffset = header.valueCount;
        header.pointSize += field.size * field.count;
        header.valueCount += field.count;
        header.fields.push_back(field);
    }
    if (header.data != "ascii" && header.data != "binary") {
        throw std::runtime_error("Unsupported PCD DATA encoding '" + header.data + "': " + filename);
    }
    return header;
}

const PcdField& xyzField(const PcdHeader& header, const char* name, const std::string& filename)
{
    const auto found = std::find_if(header.fields.begin(), header.fields.end(), [name](const PcdField& field) {
        return field.name == name;
    });
    if (found == header.fields.end() || found->count != 1) {
        throw std::runtime_error("PCD file is missing scalar XYZ fields: " + filename);
    }
    return *found;
}

template<typename T>
T readScalar(const char* bytes)
{
    T value{};
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

double readBinaryNumber(const char* bytes, const PcdField& field, const std::string& filename)
{
    if (field.type == 'F' && field.size == 4) return readScalar<float>(bytes);
    if (field.type == 'F' && field.size == 8) return readScalar<double>(bytes);
    if (field.type == 'I' && field.size == 1) return readScalar<std::int8_t>(bytes);
    if (field.type == 'I' && field.size == 2) return readScalar<std::int16_t>(bytes);
    if (field.type == 'I' && field.size == 4) return readScalar<std::int32_t>(bytes);
    if (field.type == 'I' && field.size == 8) return static_cast<double>(readScalar<std::int64_t>(bytes));
    if (field.type == 'U' && field.size == 1) return readScalar<std::uint8_t>(bytes);
    if (field.type == 'U' && field.size == 2) return readScalar<std::uint16_t>(bytes);
    if (field.type == 'U' && field.size == 4) return readScalar<std::uint32_t>(bytes);
    if (field.type == 'U' && field.size == 8) return static_cast<double>(readScalar<std::uint64_t>(bytes));
    throw std::runtime_error("Unsupported PCD numeric field type: " + filename);
}

std::vector<Eigen::Vector3d> loadPcd(const std::string& filename)
{
    std::ifstream input{std::filesystem::path(filename), std::ios::binary};
    if (!input) throw std::runtime_error("Failed to read point cloud PCD: " + filename);

    const auto header = readPcdHeader(input, filename);
    const auto& xField = xyzField(header, "x", filename);
    const auto& yField = xyzField(header, "y", filename);
    const auto& zField = xyzField(header, "z", filename);
    std::vector<Eigen::Vector3d> points;
    points.reserve(header.points);

    if (header.data == "ascii") {
        std::string line;
        std::size_t records = 0;
        while (records < header.points && std::getline(input, line)) {
            const auto first = line.find_first_not_of(" \t\r");
            if (first == std::string::npos || line[first] == '#') continue;
            std::istringstream stream(line);
            std::vector<double> values;
            values.reserve(header.valueCount);
            double value = 0.0;
            while (stream >> value) values.push_back(value);
            if (values.size() != header.valueCount) {
                throw std::runtime_error("Invalid PCD ASCII point record: " + filename);
            }
            const double x = values[xField.valueOffset];
            const double y = values[yField.valueOffset];
            const double z = values[zField.valueOffset];
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) points.emplace_back(x, y, z);
            ++records;
        }
        if (records != header.points) {
            throw std::runtime_error("PCD file ended before all points were read: " + filename);
        }
    } else {
        if (header.pointSize == 0 || header.points > std::numeric_limits<std::size_t>::max() / header.pointSize) {
            throw std::runtime_error("Invalid PCD binary size: " + filename);
        }
        std::vector<char> record(header.pointSize);
        for (std::size_t index = 0; index < header.points; ++index) {
            input.read(record.data(), static_cast<std::streamsize>(record.size()));
            if (input.gcount() != static_cast<std::streamsize>(record.size())) {
                throw std::runtime_error("PCD file ended before all points were read: " + filename);
            }
            const double x = readBinaryNumber(record.data() + xField.byteOffset, xField, filename);
            const double y = readBinaryNumber(record.data() + yField.byteOffset, yField, filename);
            const double z = readBinaryNumber(record.data() + zField.byteOffset, zField, filename);
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) points.emplace_back(x, y, z);
        }
    }
    return points;
}

} // namespace

std::vector<Eigen::Vector3d> PointCloudService::load(const std::string& filename) const
{
    std::string extension = std::filesystem::path(filename).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    std::vector<Eigen::Vector3d> points;
    if (extension == ".pcd") {
        points = loadPcd(filename);
    } else if (extension == ".txt" || extension == ".xyz") {
        std::ifstream input{std::filesystem::path(filename)};
        if (!input) throw std::runtime_error("Failed to read point cloud text file: " + filename);

        std::string line;
        int lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            const auto commentPosition = line.find('#');
            if (commentPosition != std::string::npos) line.erase(commentPosition);
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
                    throw std::runtime_error("Invalid point cloud data at line " + std::to_string(lineNumber) + ": " + filename);
                }
                continue;
            }
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) points.emplace_back(x, y, z);
        }
    } else {
        throw std::runtime_error("Unsupported point cloud format: " + extension);
    }

    if (points.empty()) throw std::runtime_error("Point cloud file contains no valid XYZ points: " + filename);
    Logger::instance().info("PointCloud", "Point cloud loaded: " + filename + ", points=" + std::to_string(points.size()));
    return points;
}

std::vector<Eigen::Vector3d> PointCloudService::mergeFrames(const std::vector<FrameReconstructionResult>& frames) const
{
    size_t count = 0;
    for (const auto& frame : frames) count += frame.points.size();
    std::vector<Eigen::Vector3d> merged;
    merged.reserve(count);
    for (const auto& frame : frames) merged.insert(merged.end(), frame.points.begin(), frame.points.end());
    return merged;
}

void PointCloudService::saveTxt(const std::string& filename, const std::vector<Eigen::Vector3d>& points, bool logSave) const
{
    ensureParentDirectory(filename);
    std::ofstream out{std::filesystem::path(filename), std::ios::trunc};
    if (!out) throw std::runtime_error("Failed to write point cloud txt: " + filename);
    for (const auto& point : points) out << point.x() << ' ' << point.y() << ' ' << point.z() << '\n';
    out.close();
    if (!out) throw std::runtime_error("Failed to finish writing point cloud txt: " + filename);
    if (logSave) Logger::instance().info("PointCloud", "TXT point cloud saved: " + filename);
}

void PointCloudService::savePcd(const std::string& filename, const std::vector<Eigen::Vector3d>& points, bool logSave) const
{
    ensureParentDirectory(filename);
    std::ofstream out{std::filesystem::path(filename), std::ios::binary | std::ios::trunc};
    if (!out) throw std::runtime_error("Failed to write point cloud pcd: " + filename);

    out << "# .PCD v0.7 - Point Cloud Data file format\n"
        << "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\n"
        << "WIDTH " << points.size() << "\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\n"
        << "POINTS " << points.size() << "\nDATA binary\n";
    for (const auto& point : points) {
        const std::array<float, 3> values{static_cast<float>(point.x()), static_cast<float>(point.y()), static_cast<float>(point.z())};
        out.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(sizeof(values)));
    }
    out.close();
    if (!out) throw std::runtime_error("Failed to finish writing point cloud pcd: " + filename);
    if (logSave) Logger::instance().info("PointCloud", "PCD point cloud saved: " + filename);
}

} // namespace htmsr
