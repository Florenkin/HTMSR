#include "app/ui/MainWindow.h"
#include "app/services/FileLogSink.h"
#include "app/services/ConfigFiles.h"
#include "core/Types.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaType>

#if HTMSR_WITH_VTK_VIEWER
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>
#endif

int main(int argc, char* argv[])
{
    const QString executablePath = argc > 0 ? QString::fromLocal8Bit(argv[0]) : QString();
    const QString applicationDirectory = executablePath.isEmpty()
        ? QDir::currentPath()
        : QFileInfo(executablePath).absolutePath();
    const auto configuredPluginRoot = htmsr::app::ConfigFiles::environmentPath("runtime/qtPluginDirectory");
    const auto platformDirectory = QDir(applicationDirectory).filePath("platforms");
    const auto selectedPluginRoot = QDir(platformDirectory).exists() || configuredPluginRoot.isEmpty()
        ? applicationDirectory : configuredPluginRoot;
    const QByteArray pluginRoot = QDir::toNativeSeparators(selectedPluginRoot).toLocal8Bit();
    const QByteArray platformPluginPath = QDir::toNativeSeparators(QDir(selectedPluginRoot).filePath("platforms")).toLocal8Bit();
    htmsr::app::ConfigFile environment(htmsr::app::ConfigFiles::path("environment.ini"));
    QStringList dllDirectories;
    for (const auto& directory : environment.value("runtime/extraDllDirectories", QString()).toString().split(';', Qt::SkipEmptyParts))
        dllDirectories.append(QDir::toNativeSeparators(htmsr::app::ConfigFiles::resolvePath(directory.trimmed())));
    if (!dllDirectories.isEmpty())
        qputenv("PATH", (dllDirectories.join(';') + ';' + qEnvironmentVariable("PATH")).toLocal8Bit());

    // Qt Widgets 程序入口，先锁定当前可执行文件目录下的插件路径，避免 VS 调试环境误加载全局 Qt DLL 和插件。
    qputenv("QT_PLUGIN_PATH", pluginRoot);
    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", platformPluginPath);

#if HTMSR_WITH_VTK_VIEWER
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
#endif

    QApplication app(argc, argv);
    // 相对的工程输入和输出路径固定以 config 的父目录为基准。
    QDir::setCurrent(QFileInfo(htmsr::app::ConfigFiles::directory()).absolutePath());
    // LogMessage 会跨线程通过 Qt signal 传递，需要注册元类型。
    qRegisterMetaType<htmsr::LogMessage>("htmsr::LogMessage");

    app.setLibraryPaths({
        applicationDirectory,
        configuredPluginRoot,
        QDir(applicationDirectory).filePath("platforms"),
        QDir(applicationDirectory).filePath("styles")
    });

    htmsr::app::AppConfigService bootstrap;
    bootstrap.load();
    htmsr::app::ConfigFile paths(htmsr::app::ConfigFiles::path("paths.ini"));
    const auto logDirectory = htmsr::app::ConfigFiles::resolvePath(paths.value("paths/logDirectory", QStringLiteral("../log")).toString());
    htmsr::app::FileLogSink fileLog(logDirectory);
    if (!environment.error().isEmpty())
        htmsr::Logger::instance().warning("Config", environment.error().toStdString());
    int exitCode = 0;
    {
        // 文件日志先于窗口启动，并持续到后台任务和窗口完成销毁。
        htmsr::app::MainWindow window;
        window.show();
        auto& logger = htmsr::Logger::instance();
        if (fileLog.isActive()) {
            logger.info("Logging", "运行日志：" + fileLog.filePath().toStdString());
        } else {
            logger.error("Logging", "无法创建运行日志：" + fileLog.lastError().toStdString());
        }
        if (fileLog.removedFileCount() > 0) {
            logger.info("Logging", "已清理超过一天的日志文件：" + std::to_string(fileLog.removedFileCount()) + " 个");
        }
        if (!fileLog.cleanupFailures().isEmpty()) {
            logger.warning("Logging", "以下过期日志无法清理：" + fileLog.cleanupFailures().join(", ").toStdString());
        }
        exitCode = app.exec();
    }
    htmsr::Logger::instance().info("App", "HTMSR stopped.");
    return exitCode;
}
