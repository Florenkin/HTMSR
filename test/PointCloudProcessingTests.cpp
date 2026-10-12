#include "QtTestApplication.h"
#include "app/ui/PointCloudSceneWidget.h"
#include "app/ui/PointCloudViewWidget.h"
#include "core/PointCloudService.h"
#include <Eigen/Geometry>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDataStream>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace htmsr;
using namespace htmsr::app;
namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void close(double actual, double expected, double tolerance, const char* message)
{
    require(std::abs(actual - expected) < tolerance, message);
}
CloudPoints sphere(const Eigen::Vector3d& center, double radius)
{
    CloudPoints points;
    for (int latitude = 1; latitude < 12; ++latitude)
        for (int angle = 0; angle < 24; ++angle)
        {
            const double a = angle * 2 * 3.141592653589793 / 24, t = latitude * 3.141592653589793 / 12;
            points.push_back(center
                + radius
                    * Eigen::Vector3d(std::sin(t) * std::cos(a), std::sin(t) * std::sin(a), std::cos(t)));
        }
    return points;
}
void algorithms()
{
    PointCloudProcessingService service;
    CloudPoints points = { { .1, .2, .3 }, { .3, .4, .5 }, { -0.1, 0, 0 }, { 2, 0, 0 },
        { std::numeric_limits<double>::quiet_NaN(), 0, 0 } };
    require(service.filterFinite(points).size() == 4, "Invalid coordinates must be removed");
    const auto voxel = service.voxelFilter(points, 1);
    require(voxel.size() == 3, "Voxel grouping must respect negative coordinates");
    close(voxel[1].x(), .2, 1e-12, "Voxel output must be the cell centroid");
    require(service.passThrough(points, 0, 0, 1).size() == 2,
        "Pass-through must use the selected axis and inclusive range");
    CloudPoints plane;
    for (int x = -10; x <= 10; ++x)
        for (int y = -10; y <= 10; ++y)
            plane.emplace_back(x, y, 2);
    auto noisy = plane;
    noisy.emplace_back(100, 100, 100);
    const auto filtered = service.statisticalFilter(noisy, 12, 1);
    require(filtered.size() == plane.size(),
        "Statistical filter must reject an isolated outlier and preserve the grid");
    const auto planeFit = service.fit(plane, CloudFitKind::Plane);
    close(planeFit.offset, -2, 1e-9, "Plane offset must agree with the input surface");
    close(planeFit.statistics.rmse, 0, 1e-9, "Exact plane residuals must be zero");
    const Eigen::Vector3d center(1e6, -2e6, 3e6);
    const auto ball = service.fit(sphere(center, 2), CloudFitKind::Sphere, 4.2);
    require((ball.center - center).norm() < 1e-6,
        "Sphere fitting must remain stable at large translated coordinates");
    close(ball.radius, 2, 1e-6, "Sphere radius must be accurate");
    close(ball.statistics.mean, -.1, 1e-6, "Standard diameter errors must use the selected reference radius");
    auto outliers = sphere(center, 2);
    for (int i = 0; i < 12; ++i)
        outliers.push_back(center + Eigen::Vector3d(50 + i, 70, -60));
    const auto robustBall = service.fit(outliers, CloudFitKind::Sphere);
    close(robustBall.radius, 2, 1e-5, "Sphere reference must resist a minority of remote outliers");
    const Eigen::Matrix3d rotation
        = Eigen::AngleAxisd(.7, Eigen::Vector3d(1, 2, 3).normalized()).toRotationMatrix();
    for (double height : { 2.0, 20.0 })
    {
        CloudPoints cylinder;
        for (int z = 0; z < 15; ++z)
            for (int angle = 0; angle < 36; ++angle)
            {
                const double a = angle * 2 * 3.141592653589793 / 36;
                cylinder.push_back(
                    rotation * Eigen::Vector3d(2 * std::cos(a), 2 * std::sin(a), height * (z / 14.0 - .5))
                    + Eigen::Vector3d(10, -3, 4));
            }
        const auto fit = service.fit(cylinder, CloudFitKind::Cylinder);
        close(fit.radius, 2, 1e-5, "Rotated short and long cylinders must recover the radius");
        close(std::abs(fit.direction.dot(rotation.col(2))), 1, 1e-5,
            "Cylinder axis must not rely on longest PCA extent");
        close(fit.statistics.rmse, 0, 1e-5, "Exact cylinder residuals must vanish");
    }
    CloudPoints polynomial;
    for (int x = -10; x <= 10; ++x)
        for (int y = -10; y <= 10; ++y)
        {
            const double u = x * .1, v = y * .1;
            polynomial.emplace_back(u, v, .03 * u * u + .02 * v * v + .01 * std::pow(u, 4));
        }
    const auto quadric = service.fit(polynomial, CloudFitKind::Quadric),
               quartic = service.fit(polynomial, CloudFitKind::Quartic);
    require(quartic.statistics.rmse < 1e-8 && quartic.statistics.rmse < quadric.statistics.rmse,
        "Quartic fit must recover a fourth-degree height surface");
    auto first = sphere({ -8, 0, 0 }, 2), second = sphere({ 8, 0, 0 }, 3);
    first.insert(first.end(), second.begin(), second.end());
    const auto doubleBall = service.fitDoubleSphere(first);
    require(doubleBall.size() == 2, "Double sphere fitting must produce two models");
    close((doubleBall[0].center - doubleBall[1].center).norm(), 16, 1e-5,
        "Double sphere center distance must be accurate");
    CloudPoints damaged;
    for (int x = -10; x <= 10; ++x)
        for (int y = -10; y <= 10; ++y)
        {
            double z = 2;
            if (x >= -7 && x <= -5 && y >= -1 && y <= 1)
                z = 0;
            if (x >= 5 && x <= 7 && y >= -1 && y <= 1)
                z = 4;
            damaged.emplace_back(x, y, z);
        }
    const auto reference = service.fit(damaged, CloudFitKind::Plane);
    const auto defects = service.measureDefects(damaged, reference, 1, 1.5, 3);
    require(defects.size() == 2, "Separated depression and protrusion must form distinct connected defects");
    for (const auto& defect : defects)
    {
        require(defect.indices.size() == 9, "Each defect must retain the original point indices");
        close(defect.maximumDepth, 2, 1e-6, "Defect depth must use the robust reference plane");
    }
    close(service.estimateSpacing(plane), 1, 1e-9, "Spacing must use nearest neighbors");
    bool rejected = false;
    try
    {
        service.fit({ { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } }, CloudFitKind::Plane);
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    require(rejected, "Degenerate geometry must not produce a plausible fit");
    CancellationToken cancelled;
    cancelled.request();
    rejected = false;
    try
    {
        service.statisticalFilter(plane, 12, 1, cancelled);
    }
    catch (const OperationCancelled&)
    {
        rejected = true;
    }
    require(rejected, "Long-running processing must respect cancellation");
    QTemporaryDir directory;
    const auto filename = directory.filePath(QString::fromUtf8("点云.ply"));
    PointCloudService io;
    io.savePly(filename.toStdString(), plane, false);
    const auto roundtrip = io.load(filename.toStdString());
    require(roundtrip.size() == plane.size(), "PLY roundtrip must preserve points and Unicode filenames");
    require((roundtrip.back() - plane.back()).norm() < 1e-12,
        "PLY roundtrip must preserve full coordinate precision");
    const auto text = directory.filePath(QString::fromUtf8("中文目录/点云.txt"));
    const auto pcd = directory.filePath(QString::fromUtf8("中文目录/点云.pcd"));
    io.saveTxt(text.toStdString(), plane, false);
    io.savePcd(pcd.toStdString(), plane, false);
    require(io.load(text.toStdString()).size() == plane.size()
            && io.load(pcd.toStdString()).size() == plane.size(),
        "Imported and saved clouds must support Unicode names in every supported format");
    const auto binaryFile = directory.filePath("external_binary.ply");
    QFile binary(binaryFile);
    require(binary.open(QIODevice::WriteOnly), "Binary PLY fixture must be writable");
    binary.write("ply\nformat binary_little_endian 1.0\nelement vertex 1\nproperty uchar red\nproperty float "
                 "z\nproperty float x\nproperty float y\nend_header\n");
    QDataStream stream(&binary);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << quint8(255) << float(3) << float(1) << float(2);
    binary.close();
    const auto binaryPoints = io.load(binaryFile.toStdString());
    require(binaryPoints.size() == 1 && (binaryPoints[0] - Eigen::Vector3d(1, 2, 3)).norm() < 1e-9,
        "Binary PLY must support reordered XYZ and extra color properties");
    const auto invalidFile = directory.filePath("malformed.ply");
    QFile invalid(invalidFile);
    require(invalid.open(QIODevice::WriteOnly), "Malformed PLY fixture must be writable");
    invalid.write("ply\nformat ascii 1.0\nelement vertex invalid_count\nend_header\n");
    invalid.close();
    rejected = false;
    try
    {
        io.load(invalidFile.toStdString());
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    require(
        rejected, "Malformed PLY declarations must fail deterministically without reading invalid counts");
}
void trigger(PointCloudViewWidget& view, const char* name)
{
    auto* action = view.findChild<QAction*>(name);
    require(action != nullptr, "Expected point cloud action is missing");
    action->trigger();
}
void wait(PointCloudViewWidget& view)
{
    QElapsedTimer timer;
    timer.start();
    while (view.isProcessing() && timer.elapsed() < 10000)
    {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    require(!view.isProcessing(), "Background point cloud processing did not finish");
    QApplication::processEvents();
}
void ui(const QString& screenshot)
{
    PointCloudViewWidget view;
    view.resize(1350, 900);
    view.show();
    CloudPoints points;
    for (int x = -10; x <= 10; ++x)
        for (int y = -10; y <= 10; ++y)
            points.emplace_back(x, y, 2);
    view.setPoints(points);
    QApplication::processEvents();
    trigger(view, "cloudSelectAll");
    require(view.selectedPointCount() == points.size(), "Select all must use the full point cloud");
    view.selectIndices({ 0, 1, 2 });
    trigger(view, "cloudInvertSelection");
    require(
        view.selectedPointCount() == points.size() - 3, "Invert selection must preserve full-data indices");
    trigger(view, "cloudSelectNone");
    auto* tools = view.findChild<QComboBox*>("cloudSelectionTool");
    tools->setCurrentIndex(1);
    QWidget* overlay = view.findChild<QWidget*>("cloudSelectionOverlay");
    require(overlay != nullptr, "Selection overlay missing");
    const QPoint a(1, 1), b(overlay->width() - 2, overlay->height() - 2);
    QMouseEvent press(QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(overlay, &press);
    QApplication::sendEvent(overlay, &move);
    QApplication::sendEvent(overlay, &release);
    require(
        view.selectedPointCount() == points.size(), "Rectangle selection must project every source point");
    tools->setCurrentIndex(0);
    view.selectIndices({ 0, 1, 2 });
    trigger(view, "cloudEdit0");
    require(view.currentPoints().size() == points.size() - 3,
        "Delete selected must create an edited point cloud");
    auto* tree = view.findChild<QTreeWidget*>("pointCloudList");
    require(tree->topLevelItemCount() == 2, "Selection editing must preserve the original cloud");
    tree->setCurrentItem(tree->topLevelItem(0));
    require(
        view.currentPoints().size() == points.size(), "Original points must remain available for recovery");
    view.selectIndices({ 0, 1, 21, 22 });
    trigger(view, "cloud_plane");
    wait(view);
    require(
        view.currentPoints().size() == 4, "Fitting must use only selected points when a selection exists");
    auto* report = view.findChild<QPlainTextEdit*>();
    require(report->toPlainText().contains("RMSE"), "Fit report must be shown in the point cloud page");
    tree->setCurrentItem(tree->topLevelItem(0));
    view.selectIndices({ 0, 1, 2 });
    const int before = tree->topLevelItemCount();
    trigger(view, "cloud_plane");
    wait(view);
    require(tree->topLevelItemCount() == before && view.currentPoints().size() == points.size(),
        "Failed fit must preserve cloud state without creating an invalid result");
    tree->clearSelection();
    tree->topLevelItem(0)->setSelected(true);
    tree->topLevelItem(1)->setSelected(true);
    trigger(view, "cloudMerge");
    require(view.currentPoints().size() == points.size() * 2 - 3,
        "Merge must combine selected cloud entries without losing points");
    trigger(view, "cloud_plane");
    wait(view);
    if (!screenshot.isEmpty())
        require(view.grab().save(screenshot), "Point cloud UI screenshot could not be saved");
    tree->setCurrentItem(tree->topLevelItem(0));
    view.selectIndices({ 0, 1, 21, 22 });
    bool dialogHandled = false;
    QTimer::singleShot(0, &view,
        [&]
        {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (dialog)
            {
                // Simulate arriving reconstruction results while a parameter dialog is open.
                for (int i = 0; i < 20; ++i)
                    view.setPoints({ { 100.0 + i, 0, 30 }, { 100.0 + i, 1, 30 } });
                dialogHandled = true;
                dialog->accept();
            }
        });
    trigger(view, "cloud_voxel");
    wait(view);
    require(dialogHandled && view.currentPoints().size() == 4 && view.currentPoints()[0].z() == 2,
        "An arriving reconstruction must not invalidate or replace a dialog's original processing source");
    QTimer::singleShot(0, &view,
        [&]
        {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (dialog)
            {
                view.clear();
                dialog->accept();
            }
        });
    trigger(view, "cloud_voxel");
    require(!view.isProcessing() && view.currentPoints().empty(),
        "Removing a cloud while its parameter dialog is open must safely abandon the operation");
    view.clear();
    require(view.currentPoints().empty(), "Clear must release point clouds");
}
}
int main(int argc, char** argv)
{
    htmsr::test::configureQtTestApplication(argv[0]);
    QApplication app(argc, argv);
    try
    {
        algorithms();
        ui(app.arguments().size() > 1 ? app.arguments()[1] : QString { });
        std::cout << "PASS: filters, geometric and polynomial fits, defects, Unicode PLY, selection, "
                     "original preservation, async results and merge\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "FAIL: " << ex.what() << '\n';
        return 1;
    }
}
