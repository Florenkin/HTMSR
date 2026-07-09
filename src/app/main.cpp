#include "app/ui/MainWindow.h"
#include "core/Types.h"

#include <QApplication>
#include <QMetaType>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    qRegisterMetaType<htmsr::LogMessage>("htmsr::LogMessage");

    htmsr::app::MainWindow window;
    window.show();
    return app.exec();
}
