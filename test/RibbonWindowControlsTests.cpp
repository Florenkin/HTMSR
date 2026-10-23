#include "QtTestApplication.h"

#include <SARibbon.h>
#include <QAbstractButton>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QLabel>
#include <QPixmap>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// 保持关闭事件可观察，验证系统按钮不会绕过主窗口的任务退出/取消流程。
class RibbonTestWindow final : public SARibbonMainWindow {
public:
    bool acceptClose = false;
    int closeRequests = 0;

protected:
    void closeEvent(QCloseEvent* event) override
    {
        ++closeRequests;
        if (acceptClose)
            event->accept();
        else
            event->ignore();
    }
};

/*
    函数功能：检查按钮中心是否实际绘制了符号，避免只验证资源存在而漏掉空白按钮
    输入：
        button：已经显示的标题栏按钮；截图坐标使用设备像素
    输出：
        返回值：中心区域存在足够的颜色或透明度对比像素时为 true
*/
bool hasVisibleGlyph(QAbstractButton* button)
{
    const QImage image = button->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    if (image.isNull())
        return false;
    const QColor background = image.pixelColor(2, 2);
    int contrastPixels = 0;
    // 只检查中心区域，避免把按钮边框当成图标；同时兼容暗色符号及悬停时的白色符号。
    for (int y = image.height() / 4; y < image.height() * 3 / 4; ++y)
        for (int x = image.width() / 4; x < image.width() * 3 / 4; ++x) {
            const QColor color = image.pixelColor(x, y);
            const int contrast = std::max({std::abs(color.red() - background.red()),
                std::abs(color.green() - background.green()), std::abs(color.blue() - background.blue()),
                std::abs(color.alpha() - background.alpha())});
            if (contrast > 70)
                ++contrastPixels;
        }
    return contrastPixels >= 3;
}

void savePreview(RibbonTestWindow& window, const QString& directory, const QString& state)
{
    if (directory.isEmpty())
        return;
    require(QDir().mkpath(directory), "Preview directory must be writable");
    require(window.grab().save(QDir(directory).filePath(state + ".png")), "Window preview must save");
    require(window.windowButtonBar()->grab().save(QDir(directory).filePath(state + "-controls.png")),
        "Title-bar controls preview must save");
}
}

int main(int argc, char** argv)
{
    htmsr::test::configureQtTestApplication(argv[0]);
    SARibbonBar::initHighDpi();
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    // 只允许已部署的插件；不能借用开发机 Qt 安装目录掩盖发布目录漏文件。
    application.setLibraryPaths({QCoreApplication::applicationDirPath()});

    try {
        RibbonTestWindow window;
        window.setWindowTitle(QString::fromUtf8("HTMSR 窗口按钮验证 · 不连接硬件"));
        window.resize(1040, 480);
        auto* ribbon = window.ribbonBar();
        ribbon->setObjectName(QStringLiteral("mainRibbon"));
        ribbon->setRibbonStyle(SARibbonBar::RibbonStyleCompactThreeRow);
        ribbon->setTitleBarHeight(40);
        ribbon->setTabBarHeight(34);
        ribbon->addCategoryPage(QString::fromUtf8("窗口图标验证"));
        window.setRibbonTheme(SARibbonTheme::RibbonThemeOffice2013);
        QFile theme(QStringLiteral(":/htmsr/theme/light.qss"));
        require(theme.open(QIODevice::ReadOnly), "Production window theme must be available");
        window.setStyleSheet(window.styleSheet() + QString::fromUtf8(theme.readAll()));
        auto* message = new QLabel(QString::fromUtf8(
            "仅验证最小化、最大化/还原和关闭按钮。\n不访问相机、串口、配置或采集数据。"));
        message->setAlignment(Qt::AlignCenter);
        window.setCentralWidget(message);
        window.show();
        application.processEvents();

        const auto arguments = application.arguments();
        const int previewIndex = arguments.indexOf("--render-preview");
        const QString previewDirectory = previewIndex >= 0 && previewIndex + 1 < arguments.size()
            ? arguments[previewIndex + 1] : QString{};
        savePreview(window, previewDirectory, "normal");

        const char* assets[] = {"Titlebar_Min.svg", "Titlebar_Min_Hover.svg", "Titlebar_Max.svg",
            "Titlebar_Max_Hover.svg", "Titlebar_Normal.svg", "Titlebar_Normal_Hover.svg",
            "Titlebar_Close.svg", "Titlebar_Close_Hover.svg"};
        for (const auto* asset : assets) {
            QImageReader reader(QStringLiteral(":/SARibbon/image/resource/%1").arg(QString::fromLatin1(asset)));
            const QImage image = reader.read();
            if (image.isNull())
                throw std::runtime_error(std::string("Cannot decode title-bar SVG ") + asset + ": "
                    + reader.errorString().toStdString());
            bool hasInk = false;
            for (int y = 0; y < image.height() && !hasInk; ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (image.pixelColor(x, y).alpha() > 0) {
                        hasInk = true;
                        break;
                    }
            require(hasInk, "Title-bar SVG must contain a visible glyph");
        }
        std::cout << "PASS: all normal, hover and restore SVG assets decode using deployed plugins\n";

        auto* buttons = window.windowButtonBar();
        require(buttons && buttons->minimizeButton() && buttons->maximizeButton() && buttons->closeButton(),
            "All three title-bar controls must be present");
        for (auto* button : {buttons->minimizeButton(), buttons->maximizeButton(), buttons->closeButton()}) {
            require(button->isVisible() && button->isEnabled(), "Title-bar controls must be visible and enabled");
            require(hasVisibleGlyph(button), "Visible title-bar controls must draw their glyphs, not blank buttons");
        }
        if (arguments.contains("--interactive-preview")) {
            window.acceptClose = true;
            application.setQuitOnLastWindowClosed(true);
            return application.exec();
        }

        buttons->maximizeButton()->click();
        application.processEvents();
        require(window.isMaximized() && buttons->maximizeButton()->isChecked(),
            "Maximize must update both the window and the restore icon state");
        require(hasVisibleGlyph(buttons->maximizeButton()), "The restore icon must remain visible");
        savePreview(window, previewDirectory, "maximized");
        buttons->maximizeButton()->click();
        application.processEvents();
        require(!window.isMaximized() && !buttons->maximizeButton()->isChecked(),
            "Restore must return to the normal window and maximize icon state");
        require(hasVisibleGlyph(buttons->maximizeButton()), "The maximize icon must recover after restore");
        savePreview(window, previewDirectory, "restored");

        window.showMaximized();
        application.processEvents();
        require(buttons->maximizeButton()->isChecked(), "External window state changes must update the restore icon");
        window.showNormal();
        application.processEvents();
        buttons->minimizeButton()->click();
        application.processEvents();
        require(window.isMinimized(), "Minimize must minimize the window");
        window.showNormal();
        application.processEvents();

        buttons->closeButton()->click();
        application.processEvents();
        require(window.closeRequests == 1 && window.isVisible(),
            "Close must respect a window's rejected close event rather than forcing exit");
        window.acceptClose = true;
        buttons->closeButton()->click();
        application.processEvents();
        require(window.closeRequests == 2 && !window.isVisible(), "Accepted close must close the window");
        std::cout << "PASS: minimize, maximize, restore, external state changes and guarded close\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
