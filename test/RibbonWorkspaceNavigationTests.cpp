#include "QtTestApplication.h"
#include "app/ui/RibbonWorkspaceNavigation.h"
#include "app/ui/PointCloudViewWidget.h"

#include <SARibbon.h>
#include <QApplication>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPixmap>
#include <QTabBar>
#include <QTabWidget>
#include <QVector>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

int main(int argc, char** argv)
{
    htmsr::test::configureQtTestApplication(argv[0]);
    SARibbonBar::initHighDpi();
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    application.setLibraryPaths({QCoreApplication::applicationDirPath()});
    try {
        SARibbonMainWindow window;
        window.setWindowTitle(QString::fromUtf8("HTMSR 单排导航验证 · 不连接硬件"));
        window.resize(1440, 680);
        auto* ribbon = window.ribbonBar();
        ribbon->setObjectName(QStringLiteral("mainRibbon"));
        ribbon->setRibbonStyle(SARibbonBar::RibbonStyleCompactThreeRow);
        ribbon->setTitleBarHeight(40);
        ribbon->setTabBarHeight(34);
        ribbon->showMinimumModeButton(true);
        auto* workspaces = new QTabWidget;
        workspaces->setObjectName(QStringLiteral("centralTabs"));
        workspaces->setDocumentMode(true);
        const QStringList workspaceNames = {QString::fromUtf8("双目"), QString::fromUtf8("点云"),
            QString::fromUtf8("采集"), QString::fromUtf8("激光线"), QString::fromUtf8("命令")};
        QVector<QWidget*> originalPages;
        htmsr::app::PointCloudViewWidget* cloud = nullptr;
        for (const auto& name : workspaceNames) {
            QWidget* page = nullptr;
            if (originalPages.size() == 1) {
                cloud = new htmsr::app::PointCloudViewWidget;
                page = cloud;
            } else {
                auto* label = new QLabel(name + QString::fromUtf8("工作区\n仅验证页面切换，不访问硬件或用户配置。"));
                label->setAlignment(Qt::AlignCenter);
                page = label;
            }
            workspaces->addTab(page, name);
            originalPages.append(page);
        }
        window.setCentralWidget(workspaces);
        htmsr::app::RibbonWorkspaceNavigation navigation(ribbon, workspaces, &window);
        auto* fileMenu = new SARibbonMenu(&window);
        for (const auto& name : {QStringLiteral("cloudLoad"), QStringLiteral("cloudExport")}) {
            auto* action = cloud->findChild<QAction*>(name);
            require(action != nullptr, "Real cloud actions must exist before creating the file menu");
            fileMenu->addAction(action);
        }
        auto* cloudPanel = navigation.category(QStringLiteral("cloudCategory"))->addPanel(QStringLiteral("Cloud"));
        for (const auto& name : {QStringLiteral("cloudLoad"), QStringLiteral("cloudExport"), QStringLiteral("cloudFocus")}) {
            auto* action = cloud->findChild<QAction*>(name);
            require(action != nullptr, "Real cloud actions must remain available after file-menu reuse");
            action->setIcon(QIcon(QStringLiteral(":/htmsr/icons/open.svg")));
            cloudPanel->addLargeAction(action);
        }
        QVector<int> workflowRequests;
        QObject::connect(&navigation, &htmsr::app::RibbonWorkspaceNavigation::workflowPageRequested,
            [&workflowRequests](int index) { workflowRequests.append(index); });
        window.setRibbonTheme(SARibbonTheme::RibbonThemeOffice2013);
        QFile theme(QStringLiteral(":/htmsr/theme/light.qss"));
        require(theme.open(QIODevice::ReadOnly), "Production theme must be available");
        window.setStyleSheet(window.styleSheet() + QString::fromUtf8(theme.readAll()));
        if (auto* file = qobject_cast<QToolButton*>(ribbon->applicationButton()))
            file->setText(QString::fromUtf8("文件"));
        window.show();
        application.processEvents();

        const QStringList categoryNames = {QString::fromUtf8("双目"), QString::fromUtf8("标定"),
            QString::fromUtf8("重建"), QString::fromUtf8("点云"), QString::fromUtf8("采集图像"),
            QString::fromUtf8("激光线"), QString::fromUtf8("命令"), QString::fromUtf8("视图"),
            QString::fromUtf8("帮助")};
        const auto categories = ribbon->categoryPages();
        require(categories.size() == categoryNames.size(), "All previous navigation entries must have one top-row home");
        for (int index = 0; index < categories.size(); ++index)
            require(categories[index]->windowTitle() == categoryNames[index], "Top-row navigation order must be stable");
        require(workspaces->tabBar()->isHidden(), "The duplicate central navigation row must be hidden");

        const char* pageCategories[] = {"stereoCategory", "cloudCategory", "captureCategory",
            "laserCategory", "commandCategory"};
        for (int index = 0; index < 5; ++index) {
            ribbon->setCurrentIndex(ribbon->categoryIndex(navigation.category(QString::fromLatin1(pageCategories[index]))));
            application.processEvents();
            require(workspaces->currentWidget() == originalPages[index],
                "Each workspace tab must show the original page, not recreate or clear it");
            require(workspaces->tabBar()->isHidden(), "Workspace navigation must not reveal a second row");
        }
        require(workflowRequests.isEmpty(), "Browsing data pages must not change workflows or dispatch hardware operations");

        auto* calibration = navigation.category(QStringLiteral("acquisitionCategory"));
        auto* reconstruction = navigation.category(QStringLiteral("reconstructionCategory"));
        ribbon->setCurrentIndex(ribbon->categoryIndex(calibration));
        application.processEvents();
        require(workspaces->currentIndex() == 0 && ribbon->categoryByIndex(ribbon->currentIndex()) == calibration
                && workflowRequests.size() == 1 && workflowRequests.at(0) == 0,
            "Calibration must select the live workspace and retain its own top tab without recursive switching");
        ribbon->setCurrentIndex(ribbon->categoryIndex(reconstruction));
        application.processEvents();
        require(workspaces->currentIndex() == 0 && ribbon->categoryByIndex(ribbon->currentIndex()) == reconstruction
                && workflowRequests.size() == 2 && workflowRequests.at(0) == 0 && workflowRequests.at(1) == 1,
            "Reconstruction must request the corresponding workflow exactly once");

        navigation.showWorkspace(2);
        for (const auto& name : {QStringLiteral("viewCategory"), QStringLiteral("helpCategory")}) {
            ribbon->setCurrentIndex(ribbon->categoryIndex(navigation.category(name)));
            application.processEvents();
            require(workspaces->currentWidget() == originalPages[2], "View and help tabs must retain the inspected data page");
        }
        for (int index = 4; index >= 0; --index) {
            workspaces->setCurrentIndex(index);
            application.processEvents();
            require(ribbon->categoryByIndex(ribbon->currentIndex())
                    == navigation.category(QString::fromLatin1(pageCategories[index])),
                "Programmatic workspace changes must synchronize the top navigation");
        }
        ribbon->setCurrentIndex(ribbon->categoryIndex(calibration));
        navigation.showWorkspace(0);
        require(ribbon->categoryByIndex(ribbon->currentIndex()) == navigation.category(QStringLiteral("stereoCategory")),
            "Explicit live-preview actions must select Stereo even when the underlying page is already visible");
        const int workflowCount = workflowRequests.size();
        navigation.showWorkspace(-1);
        navigation.showWorkspace(9);
        require(workspaces->currentIndex() == 0 && workflowRequests.size() == workflowCount,
            "Invalid workspace requests must not change pages or workflows");
        ribbon->setMinimumMode(true);
        navigation.showWorkspace(4);
        require(workspaces->currentIndex() == 4 && workspaces->tabBar()->isHidden(),
            "Collapsed Ribbon must still navigate without restoring duplicate tabs");
        ribbon->setMinimumMode(false);
        for (int width : {900, 1600, 1440}) {
            window.resize(width, 680);
            application.processEvents();
            require(workspaces->tabBar()->isHidden(), "Resizing must retain single-row navigation");
        }
        for (int index = 0; index < 5; ++index)
            require(workspaces->widget(index) == originalPages[index], "All original workspace objects and data must survive navigation");
        navigation.showWorkspace(0);
        std::cout << "PASS: single navigation row, five preserved workspaces, workflow aliases, utility pages, synchronization and resize\n";

        const auto arguments = application.arguments();
        const int imageIndex = arguments.indexOf("--render-preview");
        if (imageIndex >= 0 && imageIndex + 1 < arguments.size()) {
            const QString directory = arguments[imageIndex + 1];
            require(QDir().mkpath(directory), "Preview directory must be writable");
            require(window.grab().save(QDir(directory).filePath("navigation.png")), "Navigation preview must save");
        }
        if (arguments.contains("--interactive-preview")) {
            application.setQuitOnLastWindowClosed(true);
            return application.exec();
        }
        window.close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
