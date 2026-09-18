#include "app/services/FileLogSink.h"
#include "app/services/QtLogSink.h"
#include "app/ui/LogPanel.h"
#include "QtTestApplication.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QPixmap>
#include <QTableWidget>
#include <QScrollBar>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace htmsr;
using namespace htmsr::app;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void fixture(const QString& path, const QDateTime& modified)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "Temporary log fixture must be created");
    file.write("fixture\n");
    require(file.flush(), "Fixture data must be flushed before setting its time");
    require(file.setFileTime(modified, QFileDevice::FileModificationTime), "Fixture modification time must be set");
}

QString contents(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Generated log must remain readable");
    return QString::fromUtf8(file.readAll());
}

void testFileLogging()
{
    QTemporaryDir temp;
    require(temp.isValid(), "Logging tests must use a temporary directory");
    const QDateTime now(QDate(2026, 9, 17), QTime(12, 0), Qt::UTC);
    const auto directory = temp.path();
    const auto path = [&](const QString& name) { return QDir(directory).filePath(name); };
    const auto expired = path("htmsr_20260916_110000_000.log");
    const auto recent = path("htmsr_20260916_130000_000.log");
    const auto boundary = path("htmsr_20260916_120000_000.log");
    const auto unrelated = path("user_notes.log");
    const auto invalidName = path("htmsr_20269999_120000_000.log");
    const auto oldDirectory = path("htmsr_20260915_120000_000.log");
    fixture(expired, now.addSecs(-25 * 3600));
    fixture(recent, now.addSecs(-23 * 3600));
    fixture(boundary, now.addSecs(-24 * 3600));
    fixture(unrelated, now.addSecs(-48 * 3600));
    fixture(invalidName, now.addSecs(-48 * 3600));
    require(QDir().mkpath(oldDirectory), "Directory named like a log must be available");

    auto& logger = Logger::instance();
    std::atomic_int observed{0};
    const auto observer = logger.addSink([&](const LogMessage&) { ++observed; });
    QString firstFile;
    {
        FileLogSink sink(directory, now);
        require(sink.isActive() && sink.removedFileCount() == 1 && sink.cleanupFailures().isEmpty(),
            "Startup must remove exactly the owned file older than 24 hours");
        require(!QFileInfo::exists(expired) && QFileInfo::exists(recent) && QFileInfo::exists(boundary) &&
            QFileInfo::exists(unrelated) && QFileInfo::exists(invalidName) && QFileInfo(oldDirectory).isDir(),
            "Cleanup must retain recent/boundary logs, unrelated files, invalid names and directories");
        firstFile = sink.filePath();
        require(QFileInfo(firstFile).fileName() == "htmsr_20260917_120000_000.log",
            "Each run must use a timestamped log file name");
        logger.debug("Galvo", "Sent command: noisy bytes");
        logger.info("Capture", "采集完成\n文件已保存");
        logger.warning("Camera", "关键警告");
        logger.error("Capture", "关键错误");
        auto written = contents(firstFile);
        require(!written.contains("noisy bytes") && written.contains("采集完成\\n文件已保存") &&
            written.contains("[Warning] [Camera] 关键警告") && written.contains("[Error] [Capture] 关键错误") &&
            written.count('\n') == 3,
            "UTF-8 logs must retain key records, escape multiline text and omit debug details");
        {
            QtLogSink ui;
            int received = 0;
            QObject::connect(&ui, &QtLogSink::messageReceived, [&](const LogMessage&) { ++received; });
            logger.debug("Galvo", "hidden UI details");
            logger.info("App", "界面日志测试");
            require(received == 1, "The UI sink must forward key logs without debug messages");
        }
        logger.info("App", "窗口销毁后的关闭日志");
        require(contents(firstFile).contains("窗口销毁后的关闭日志"),
            "Destroying the UI sink must not unregister the file sink");
        {
            FileLogSink second(directory, now);
            require(second.isActive() && second.filePath() != firstFile &&
                QFileInfo(second.filePath()).fileName() == "htmsr_20260917_120000_000_1.log",
                "Runs with identical timestamps must never truncate an existing log");
        }
        std::vector<std::thread> workers;
        for (int thread = 0; thread < 4; ++thread) {
            workers.emplace_back([&, thread]() {
                for (int record = 0; record < 50; ++record) {
                    logger.info("Worker", "worker=" + std::to_string(thread) + ", record=" + std::to_string(record));
                }
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }
        written = contents(firstFile);
        require(written.count("[Info] [Worker]") == 200,
            "Concurrent workers must write complete records without losing or interleaving lines");
        for (int thread = 0; thread < 4; ++thread) {
            for (int record = 0; record < 50; ++record) {
                require(written.contains("worker=" + QString::number(thread) + ", record=" + QString::number(record) + '\n'),
                    "Every concurrent record must remain intact");
            }
        }
    }
    const auto finalText = contents(firstFile);
    const int previousObserved = observed;
    logger.info("App", "after file sink closes");
    require(contents(firstFile) == finalText && observed == previousObserved + 1,
        "Closing a file sink must leave unrelated sinks active without writing to the closed file");
    logger.removeSink(observer);
    const auto blockedDirectory = path("not_a_directory");
    fixture(blockedDirectory, now);
    FileLogSink failed(blockedDirectory, now);
    require(!failed.isActive() && !failed.lastError().isEmpty(),
        "Unavailable log directories must report an error without stopping the application");
    std::cout << "PASS: UTF-8 file logging, 24-hour cleanup, filename collisions, concurrent writing and sink lifetime\n";
}

void testLogPanel(QApplication& application)
{
    LogPanel panel;
    auto* table = panel.findChild<QTableWidget*>("logTable");
    require(table != nullptr, "Log table must be available");
    const QDateTime eventTime(QDate(2026, 9, 17), QTime(8, 15, 10, 123));
    LogMessage delayed{LogLevel::Info, "Capture", "采集完成\n文件已保存",
        std::chrono::system_clock::time_point(std::chrono::milliseconds(eventTime.toMSecsSinceEpoch()))};
    panel.appendMessage({LogLevel::Debug, "Galvo", "noisy details"});
    panel.appendMessage(delayed);
    panel.appendMessage({LogLevel::Warning, "Camera", "相机警告"});
    panel.appendMessage({LogLevel::Error, "Capture", "采集失败"});
    require(table->rowCount() == 3 && table->item(0, 0)->text() == "08:15:10.123" &&
        table->item(0, 3)->text() == QString::fromUtf8("采集完成 文件已保存"),
        "UI logs must retain warnings/errors and use event time rather than delayed delivery time");
    panel.resize(1180, panel.sizeHint().height());
    panel.show();
    application.processEvents();
    const int rowHeight = table->verticalHeader()->defaultSectionSize();
    require(table->viewport()->height() >= 2 * rowHeight && table->viewport()->height() < 3 * rowHeight,
        "The default panel height must show two rows without limiting history to two records");
    require(table->verticalScrollBar()->value() == table->verticalScrollBar()->maximum(),
        "The initial two-row view must automatically show the most recent records");
    const auto arguments = application.arguments();
    if (arguments.size() == 3 && arguments[1] == "--render-preview") {
        const auto imagePath = arguments[2];
        QDir().mkpath(QFileInfo(imagePath).absolutePath());
        require(panel.grab().save(imagePath), "Compact log panel preview must be saved");
    }
    panel.resize(1180, 300);
    application.processEvents();
    require(table->viewport()->height() > 2 * rowHeight, "Users must be able to expand the panel for more records");
    for (int record = 0; record < 2005; ++record) {
        panel.appendMessage({LogLevel::Info, "Progress", std::to_string(record)});
    }
    require(table->rowCount() == 2000 && table->item(1999, 3)->text() == "2004",
        "Long runs must bound UI memory while retaining the most recent logs");
    panel.clear();
    require(table->rowCount() == 0, "Clearing UI history must remain available");
    std::cout << "PASS: two-row default height, expandable history, key-log filtering, event timestamps and bounded memory\n";
}
}

int main(int argc, char** argv)
{
    htmsr::test::configureQtTestApplication(argv[0]);
    QApplication application(argc, argv);
    if (application.arguments().contains("--render-preview")) {
        application.setFont(QFont("Microsoft YaHei", 10));
    }
    qRegisterMetaType<LogMessage>("htmsr::LogMessage");
    try {
        testFileLogging();
        testLogPanel(application);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << '\n';
        return 1;
    }
}
