#include "app/ui/PointCloudViewWidget.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVTKOpenGLNativeWidget.h>
#include <iostream>

// 手动运行真实 VTK 场景生命周期检查，无相机或振镜访问。
int main(int argc, char** argv)
{
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    QApplication app(argc, argv);
    int cycles = 0;
    std::function<void()> run;
    run = [&]()
    {
        auto* view = new htmsr::app::PointCloudViewWidget;
        view->resize(1200, 800);
        htmsr::CloudPoints points;
        for (int x = -20; x <= 20; ++x)
            for (int y = -20; y <= 20; ++y)
                points.emplace_back(x, y, 2);
        view->setPoints(points);
        view->show();
        QObject::connect(view, &htmsr::app::PointCloudViewWidget::processingFinished, view,
            [&, view]()
            {
                QTimer::singleShot(100, view,
                    [&, view]()
                    {
                        if (argc > 1 && cycles == 0)
                            view->grab().save(QString::fromLocal8Bit(argv[1]));
                        view->clear();
                        delete view;
                        if (++cycles < 5)
                            QTimer::singleShot(0, &app, run);
                        else
                        {
                            std::cout << "PASS: VTK render, fit, clear and destroy repeated five times\n";
                            app.quit();
                        }
                    });
            });
        QTimer::singleShot(100, view,
            [&, view]()
            {
                auto* tools = view->findChild<QComboBox*>("cloudSelectionTool");
                tools->setCurrentIndex(1);
                auto* overlay = view->findChild<QWidget*>("cloudSelectionOverlay");
                const QPoint a(1, 1), b(overlay->width() - 2, overlay->height() - 2);
                QMouseEvent press(
                    QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent release(
                    QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(overlay, &press);
                QApplication::sendEvent(overlay, &release);
                if (view->selectedPointCount() != view->currentPoints().size())
                {
                    std::cerr << "FAIL: VTK projection selection omitted source points\n";
                    app.exit(1);
                    return;
                }
                tools->setCurrentIndex(0);
                view->findChild<QAction*>("cloud_plane")->trigger();
            });
    };
    app.setQuitOnLastWindowClosed(false);
    QTimer::singleShot(0, &app, run);
    QTimer::singleShot(20000, &app,
        [&]
        {
            std::cerr << "FAIL: VTK smoke timed out\n";
            app.exit(1);
        });
    return app.exec();
}
