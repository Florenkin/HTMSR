#include "app/services/FileLogSink.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <chrono>
#include <mutex>

namespace htmsr::app {
namespace {
QString oneLine(const std::string& text)
{
    QString value = QString::fromStdString(text);
    value.replace('\r', QStringLiteral("\\r"));
    value.replace('\n', QStringLiteral("\\n"));
    return value;
}
}

struct FileLogSink::State {
    std::mutex mutex;
    QFile file;
    QString error;

    QString append(const LogMessage& message)
    {
        if (message.level == LogLevel::Debug) {
            return {};
        }
        std::lock_guard<std::mutex> lock(mutex);
        if (!file.isOpen()) {
            return {};
        }
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            message.timestamp.time_since_epoch()).count();
        const QString time = QDateTime::fromMSecsSinceEpoch(milliseconds).toString("yyyy-MM-dd HH:mm:ss.zzz");
        const auto record = QString("[%1] [%2] [%3] %4\n").arg(time)
            .arg(QString::fromStdString(toString(message.level)))
            .arg(oneLine(message.module)).arg(oneLine(message.text)).toUtf8();
        qint64 written = 0;
        while (written < record.size()) {
            const auto count = file.write(record.constData() + written, record.size() - written);
            if (count <= 0) {
                error = file.errorString();
                file.close();
                return error;
            }
            written += count;
        }
        if (!file.flush()) {
            error = file.errorString();
            file.close();
            return error;
        }
        return {};
    }
};

FileLogSink::FileLogSink(const QString& directory, const QDateTime& startTime)
    : state_(std::make_shared<State>())
{
    QDir root(directory);
    if (!root.mkpath(".")) {
        state_->error = QString::fromUtf8("无法创建日志目录：%1").arg(root.absolutePath());
        return;
    }
    const QRegularExpression ownedFile("^htmsr_(\\d{8}_\\d{6}_\\d{3})(?:_\\d+)?\\.log$");
    const auto cutoff = startTime.addSecs(-24 * 60 * 60);
    const auto files = root.entryInfoList({"htmsr_*.log"}, QDir::Files | QDir::NoSymLinks);
    for (const auto& file : files) {
        const auto match = ownedFile.match(file.fileName());
        if (!match.hasMatch() || !QDateTime::fromString(match.captured(1), "yyyyMMdd_HHmmss_zzz").isValid() ||
            !file.lastModified().isValid() || file.lastModified() >= cutoff) {
            continue;
        }
        if (QFile::remove(file.absoluteFilePath())) {
            ++removedFileCount_;
        } else {
            cleanupFailures_.append(file.fileName());
        }
    }

    const QString prefix = "htmsr_" + startTime.toString("yyyyMMdd_HHmmss_zzz");
    for (int suffix = 0; ; ++suffix) {
        const QString name = prefix + (suffix == 0 ? QString() : "_" + QString::number(suffix)) + ".log";
        state_->file.setFileName(root.absoluteFilePath(name));
        if (state_->file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            break;
        }
        if (!QFileInfo::exists(state_->file.fileName())) {
            state_->error = state_->file.errorString();
            return;
        }
    }
    sinkId_ = Logger::instance().addSink([state = state_](const LogMessage& message) {
        const QString error = state->append(message);
        if (!error.isEmpty()) {
            Logger::instance().error("Logging", "运行日志写入失败：" + error.toStdString());
        }
    });
}

FileLogSink::~FileLogSink()
{
    Logger::instance().removeSink(sinkId_);
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->file.close();
}

bool FileLogSink::isActive() const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->file.isOpen();
}

QString FileLogSink::filePath() const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->file.fileName();
}

QString FileLogSink::lastError() const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->error;
}

int FileLogSink::removedFileCount() const
{
    return removedFileCount_;
}

QStringList FileLogSink::cleanupFailures() const
{
    return cleanupFailures_;
}

} // namespace htmsr::app
