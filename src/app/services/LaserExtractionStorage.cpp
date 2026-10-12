#include "app/services/LaserExtractionStorage.h"

#include "core/Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <opencv2/imgcodecs.hpp>

#include <stdexcept>

namespace htmsr::app {
namespace {

QString timestampName(const QDateTime& timestamp)
{
    return timestamp.toString("yyyyMMdd_HHmmss_zzz");
}

QString uniqueName(const QString& base, int suffix)
{
    return suffix == 0 ? base : base + "_" + QString::number(suffix);
}

bool writePng(const QString& path, const cv::Mat& image, QString& error)
{
    if (image.empty()) {
        error = QString::fromUtf8("提线预览为空。");
        return false;
    }

    std::vector<unsigned char> encoded;
    try {
        if (!cv::imencode(".png", image, encoded)) {
            error = QString::fromUtf8("PNG 编码失败。");
            return false;
        }
    } catch (const cv::Exception& ex) {
        error = QString::fromUtf8(ex.what());
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<qint64>(encoded.size())) !=
            static_cast<qint64>(encoded.size()) ||
        !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}

} // namespace

LaserExtractionStorage::LaserExtractionStorage(const std::string& rootDirectory, const QDateTime& timestamp)
{
    result_.enabled = true;
    const QString root = QString::fromUtf8(rootDirectory.c_str()).trimmed();
    if (root.isEmpty()) {
        recordFailure("Laser extraction directory is empty.");
        return;
    }
    if (!timestamp.isValid()) {
        recordFailure("Laser extraction session timestamp is invalid.");
        return;
    }
    if (!QDir().mkpath(root)) {
        recordFailure("Failed to create laser extraction root directory: " + root.toStdString());
        return;
    }

    QDir parent(root);
    QString sessionDirectory;
    const QString base = timestampName(timestamp);
    for (int suffix = 0; ; ++suffix) {
        const QString candidate = uniqueName(base, suffix);
        if (parent.mkdir(candidate)) {
            sessionDirectory = parent.filePath(candidate);
            break;
        }
        if (!QFileInfo::exists(parent.filePath(candidate))) {
            recordFailure("Failed to create laser extraction session: " + parent.filePath(candidate).toStdString());
            return;
        }
    }

    leftDirectory_ = QDir(sessionDirectory).filePath("left");
    rightDirectory_ = QDir(sessionDirectory).filePath("right");
    if (!QDir().mkpath(leftDirectory_) || !QDir().mkpath(rightDirectory_)) {
        recordFailure("Failed to create left/right laser extraction directories: " + sessionDirectory.toStdString());
        return;
    }

    result_.sessionDirectory = sessionDirectory.toStdString();
    ready_ = true;
}

bool LaserExtractionStorage::isReady() const
{
    return ready_;
}

bool LaserExtractionStorage::savePair(int frameIndex, const cv::Mat& leftPreview, const cv::Mat& rightPreview)
{
    if (!ready_) {
        return false;
    }

    const QString fileName = QString("frame_%1.png").arg(frameIndex + 1, 6, 10, QChar('0'));
    const QString leftPath = QDir(leftDirectory_).filePath(fileName);
    const QString rightPath = QDir(rightDirectory_).filePath(fileName);
    QString error;
    if (!writePng(leftPath, leftPreview, error)) {
        recordFailure("Failed to save left laser preview " + leftPath.toStdString() + ": " + error.toStdString());
        return false;
    }
    if (!writePng(rightPath, rightPreview, error)) {
        QFile::remove(leftPath);
        recordFailure("Failed to save right laser preview " + rightPath.toStdString() + ": " + error.toStdString());
        return false;
    }

    result_.leftImagePaths.push_back(leftPath.toStdString());
    result_.rightImagePaths.push_back(rightPath.toStdString());
    return true;
}

const LaserExtractionImageResult& LaserExtractionStorage::result() const
{
    return result_;
}

void LaserExtractionStorage::recordFailure(const std::string& message)
{
    ++result_.failedPairCount;
    if (result_.warningMessage.empty()) {
        result_.warningMessage = message;
    }
    Logger::instance().warning("LaserExtraction", message);
}

} // namespace htmsr::app
