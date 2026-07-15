#include "app/ui/MainWindow.h"
#include "core/Types.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaType>

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

    QApplication app(argc, argv);
    // LogMessage 会跨线程通过 Qt signal 传递，需要注册元类型。
    qRegisterMetaType<htmsr::LogMessage>("htmsr::LogMessage");

    app.setLibraryPaths({
        applicationDirectory,
        QDir(applicationDirectory).filePath("platforms"),
        QDir(applicationDirectory).filePath("styles")
    });

    // 创建主窗口并显示，后续用户操作均由 MainWindow 分发到各业务服务。
    htmsr::app::MainWindow window;
    window.show();
    return app.exec();
}
