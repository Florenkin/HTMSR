#include "app/ui/MainWindow.h"
#include "app/ui/PointCloudViewWidget.h"
#include "app/ui/RibbonWorkspaceNavigation.h"
#include "core/Types.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

#if HTMSR_WITH_VTK_VIEWER
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>
#endif

namespace htmsr::test {
namespace {
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

/*
    函数功能：用实际应用及主窗口验证界面初始化，防止占位页面测试遗漏成员布局和动作绑定异常
    输入：
        argc、argv：测试命令行；默认离屏，不开启设备或加载用户工程
    输出：
        返回值：全部检查通过为 0，否则为 1
        副作用：只创建临时配置目录；未显示窗口，未执行 GPU 渲染或硬件操作
*/
int runMainWindowUiSmokeTest(int argc, char** argv)
{
    try {
        QTemporaryDir configuration;
        require(configuration.isValid(), "An isolated configuration directory is required");
        qputenv("HTMSR_CONFIG_DIR", configuration.path().toUtf8());
        const QString directory = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
        qputenv("QT_PLUGIN_PATH", directory.toLocal8Bit());
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QDir(directory).filePath("platforms").toLocal8Bit());
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "offscreen");
#if HTMSR_WITH_VTK_VIEWER
        QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
#endif
        SARibbonBar::initHighDpi();
        QApplication application(argc, argv);
        application.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
        application.setLibraryPaths({directory});
        qRegisterMetaType<htmsr::LogMessage>("htmsr::LogMessage");
        for (int iteration = 0; iteration < 2; ++iteration) {
            app::MainWindow window(nullptr, app::MainWindow::StartupMode::InterfaceTest);
            application.processEvents();
            auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("centralTabs"));
            auto* navigation = window.findChild<app::RibbonWorkspaceNavigation*>();
            auto* cloud = window.findChild<app::PointCloudViewWidget*>();
            require(tabs && navigation && cloud, "The actual main window must initialize all workspace controls");
            require(tabs->count() == 5 && tabs->tabBar()->isHidden(), "The actual main window must retain one navigation row");
            auto* ribbon = window.ribbonBar();
            require(ribbon->categoryPages().size() == 9, "The real Ribbon must contain all nine categories");
            const QStringList categoryNames = {QStringLiteral("stereoCategory"), QStringLiteral("cloudCategory"),
                QStringLiteral("captureCategory"), QStringLiteral("laserCategory"), QStringLiteral("commandCategory")};
            for (int index = 0; index < categoryNames.size(); ++index) {
                auto* category = navigation->category(categoryNames[index]);
                ribbon->setCurrentIndex(ribbon->categoryIndex(category));
                application.processEvents();
                require(tabs->currentIndex() == index, "Each production Ribbon tab must select the correct real workspace");
            }
            for (const auto& name : {QStringLiteral("cloudLoad"), QStringLiteral("cloudExport"), QStringLiteral("cloudFocus")}) {
                auto* action = cloud->findChild<QAction*>(name);
                require(action && !action->icon().isNull(), "Production cloud actions must be bound with available icons");
                require(action->associatedWidgets().size() >= 2, "Ribbon and cloud toolbar must share the same action");
            }
            auto* fileButton = qobject_cast<QToolButton*>(ribbon->applicationButton());
            require(fileButton && fileButton->menu(), "The production File menu must be available");
            require(fileButton->menu()->actions().contains(cloud->findChild<QAction*>(QStringLiteral("cloudLoad"))),
                "The File menu must retain the existing cloud-load action");
            require(window.close(), "The real window must close safely without configuration auto-save initialized");
            application.processEvents();
        }
        require(QDir(configuration.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(),
            "Interface-only startup must not create configuration, logs or output files");
        std::cout << "PASS: actual MainWindow initialization, real actions, navigation, repeated teardown and isolated configuration\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
}
