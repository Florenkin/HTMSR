#include "app/ui/MainWindow.h"
#include "app/services/FileLogSink.h"
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
    const QByteArray pluginRoot = QDir::toNativeSeparators(applicationDirectory).toLocal8Bit();
    const QByteArray platformPluginPath = QDir::toNativeSeparators(QDir(applicationDirectory).filePath("platforms")).toLocal8Bit();

    // Qt Widgets 程序入口，先锁定当前可执行文件目录下的插件路径，避免 VS 调试环境误加载全局 Qt DLL 和插件。
    qputenv("QT_PLUGIN_PATH", pluginRoot);
    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", platformPluginPath);

#if HTMSR_WITH_VTK_VIEWER
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
#endif

    QApplication app(argc, argv);
    // LogMessage 会跨线程通过 Qt signal 传递，需要注册元类型。
    qRegisterMetaType<htmsr::LogMessage>("htmsr::LogMessage");

    app.setLibraryPaths({
        applicationDirectory,
        QDir(applicationDirectory).filePath("platforms"),
        QDir(applicationDirectory).filePath("styles")
    });

    htmsr::app::FileLogSink fileLog;
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
