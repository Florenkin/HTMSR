#pragma once

#include "core/Logger.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <memory>

namespace htmsr::app {

// 每次运行独立写入 UTF-8 日志，启动时仅清理本软件超过 24 小时的日志文件。
class FileLogSink final {
public:
    explicit FileLogSink(const QString& directory,
        const QDateTime& startTime = QDateTime::currentDateTime());
    ~FileLogSink();
    FileLogSink(const FileLogSink&) = delete;
    FileLogSink& operator=(const FileLogSink&) = delete;

    bool isActive() const;
    QString filePath() const;
    QString lastError() const;
    int removedFileCount() const;
    QStringList cleanupFailures() const;

private:
    struct State;
    std::shared_ptr<State> state_;
    Logger::SinkId sinkId_ = 0;
    int removedFileCount_ = 0;
    QStringList cleanupFailures_;
};

} // namespace htmsr::app
