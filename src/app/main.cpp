#include "app/ui/MainWindow.h"
#include "core/Types.h"

#include <QApplication>
#include <QMetaType>

int main(int argc, char* argv[])
{
    // Qt Widgets 程序入口，负责创建应用对象并启动主事件循环。
    QApplication app(argc, argv);
    // LogMessage 会跨线程通过 Qt signal 传递，需要注册元类型。
    qRegisterMetaType<htmsr::LogMessage>("htmsr::LogMessage");

    // 创建主窗口并显示，后续用户操作均由 MainWindow 分发到各业务服务。
    htmsr::app::MainWindow window;
    window.show();
    return app.exec();
}
