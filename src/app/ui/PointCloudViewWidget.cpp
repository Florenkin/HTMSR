#include "app/ui/PointCloudViewWidget.h"
#include "app/ui/PointCloudSceneWidget.h"
#include "core/Logger.h"
#include "core/PointCloudService.h"

#include <QAction>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTextStream>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace htmsr::app
{
namespace
{
    QString coordinate(const Eigen::Vector3d& p)
    {
        return QString("%1, %2, %3").arg(p.x(), 0, 'f', 4).arg(p.y(), 0, 'f', 4).arg(p.z(), 0, 'f', 4);
    }
    QString fitName(CloudFitKind kind)
    {
        switch (kind)
        {
        case CloudFitKind::Plane:
            return QString::fromUtf8("平面");
        case CloudFitKind::Sphere:
            return QString::fromUtf8("球面");
        case CloudFitKind::Cylinder:
            return QString::fromUtf8("圆柱");
        case CloudFitKind::Quadric:
            return QString::fromUtf8("二次曲面");
        default:
            return QString::fromUtf8("四次曲面");
        }
    }
    QString fitReport(const CloudFitResult& fit)
    {
        QString text = QString::fromUtf8("%1拟合\n样本点数：%2\n中心/轴上点：%3\n法向/轴方向：%4")
                           .arg(fitName(fit.kind))
                           .arg(static_cast<qulonglong>(fit.errors.size()))
                           .arg(coordinate(fit.center))
                           .arg(coordinate(fit.direction));
        if (fit.kind == CloudFitKind::Sphere || fit.kind == CloudFitKind::Cylinder)
        {
            text += QString::fromUtf8("\n半径：%1 mm\n直径：%2 mm")
                        .arg(fit.radius, 0, 'f', 5)
                        .arg(fit.radius * 2, 0, 'f', 5);
            if (fit.standardDiameter > 0)
                text += QString::fromUtf8(
                    "\n标准直径：%1 mm\n直径偏差：%2 mm\n误差参考：标准半径（中心/轴保持拟合值）")
                            .arg(fit.standardDiameter, 0, 'f', 5)
                            .arg(fit.radius * 2 - fit.standardDiameter, 0, 'f', 5);
        }
        if (fit.kind == CloudFitKind::Cylinder)
            text += QString::fromUtf8("\n轴向长度：%1 mm").arg(fit.axisMaximum - fit.axisMinimum, 0, 'f', 5);
        if (fit.kind == CloudFitKind::Plane)
            text += QString::fromUtf8("\n平面偏移 d：%1\n与 XY / XZ / YZ 平面夹角：%2 / %3 / %4°")
                        .arg(fit.offset, 0, 'f', 6)
                        .arg(std::acos(std::clamp(std::abs(fit.direction.z()), 0.0, 1.0)) * 180
                                / 3.141592653589793,
                            0, 'f', 4)
                        .arg(std::acos(std::clamp(std::abs(fit.direction.y()), 0.0, 1.0)) * 180
                                / 3.141592653589793,
                            0, 'f', 4)
                        .arg(std::acos(std::clamp(std::abs(fit.direction.x()), 0.0, 1.0)) * 180
                                / 3.141592653589793,
                            0, 'f', 4);
        if (!fit.coefficients.empty())
        {
            text += QString::fromUtf8("\n局部高度多项式（归一化 U/V，按总次数排列）\n系数：");
            for (double value : fit.coefficients)
                text += QString::number(value, 'g', 8) + " ";
            text += QString::fromUtf8("\n曲面误差为局部法向高度差。");
        }
        return text
            + QString::fromUtf8("\n平均误差 ME：%1 mm\nRMSE：%2 mm\n最大绝对误差：%3 mm")
                  .arg(fit.statistics.mean, 0, 'f', 6)
                  .arg(fit.statistics.rmse, 0, 'f', 6)
                  .arg(fit.statistics.maximumAbsolute, 0, 'f', 6);
    }
    struct CloudEntry
    {
        int id = 0;
        QString name, source, report;
        std::shared_ptr<const CloudPoints> points;
        std::vector<bool> selection;
        std::vector<double> errors;
        std::vector<CloudFitResult> fits;
        bool visible = true;
        QColor color = QColor(35, 50, 65);
    };
    struct CloudOutput
    {
        CloudPoints points;
        QString suffix, report;
        std::vector<double> errors;
        std::vector<CloudFitResult> fits;
    };
    struct CloudTaskResult
    {
        int sourceId = -1;
        QString error;
        std::vector<CloudOutput> outputs;
    };
    QDoubleSpinBox* number(
        QFormLayout* form, const QString& title, double value, double minimum, double maximum)
    {
        auto* spin = new QDoubleSpinBox;
        spin->setDecimals(4);
        spin->setRange(minimum, maximum);
        spin->setValue(value);
        form->addRow(title, spin);
        return spin;
    }
    class ErrorHistogram final : public QWidget
    {
        std::vector<double> errors_;

    public:
        explicit ErrorHistogram(std::vector<double> errors)
            : errors_(std::move(errors))
        {
            setMinimumSize(560, 240);
        }

    protected:
        void paintEvent(QPaintEvent*) override
        {
            QPainter p(this);
            p.fillRect(rect(), Qt::white);
            if (errors_.empty())
                return;
            const auto bounds = std::minmax_element(errors_.begin(), errors_.end());
            const double low = *bounds.first, high = *bounds.second, range = std::max(1e-12, high - low);
            std::array<std::size_t, 40> bins { };
            for (double value : errors_)
                ++bins[std::clamp(int((value - low) / range * 40), 0, 39)];
            const auto peak = *std::max_element(bins.begin(), bins.end());
            const QRectF plot = rect().adjusted(45, 25, -15, -40);
            p.setPen(Qt::darkGray);
            p.drawRect(plot);
            for (int i = 0; i < 40; ++i)
            {
                const double h = plot.height() * bins[i] / std::max<std::size_t>(1, peak);
                p.fillRect(
                    QRectF(plot.left() + i * plot.width() / 40, plot.bottom() - h, plot.width() / 40 - 1, h),
                    QColor(55, 135, 210));
            }
            p.drawText(45, height() - 12, QString("%1 mm").arg(low, 0, 'f', 5));
            p.drawText(width() - 150, height() - 12, QString("%1 mm").arg(high, 0, 'f', 5));
            p.drawText(45, 18,
                QString::fromUtf8("误差分布（%1 点，40 个区间）")
                    .arg(static_cast<qulonglong>(errors_.size())));
        }
    };
} // namespace

class PointCloudViewWidget::Impl
{
public:
    QVBoxLayout* layout = nullptr;
    QToolBar* toolbar = nullptr;
    QTreeWidget* tree = nullptr;
    PointCloudSceneWidget* scene = nullptr;
    QLabel* information = nullptr;
    QLabel* state = nullptr;
    QPlainTextEdit* report = nullptr;
    QDoubleSpinBox* errorRange = nullptr;
    QComboBox* tool = nullptr;
    std::vector<CloudEntry> clouds;
    int currentId = -1, nextId = 1;
    bool busy = false, closing = false;
    QFutureWatcher<CloudTaskResult> watcher;
    CancellationToken cancellation;
    CloudEntry* current()
    {
        for (auto& entry : clouds)
            if (entry.id == currentId)
                return &entry;
        return nullptr;
    }
    const CloudEntry* current() const
    {
        for (const auto& entry : clouds)
            if (entry.id == currentId)
                return &entry;
        return nullptr;
    }
    std::vector<int> selectedIds() const
    {
        std::vector<int> ids;
        for (auto* item : tree->selectedItems())
            ids.push_back(item->data(0, Qt::UserRole).toInt());
        return ids;
    }
};

PointCloudViewWidget::PointCloudViewWidget(QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>())
{
    impl_->layout = new QVBoxLayout(this);
    impl_->layout->setContentsMargins(4, 4, 4, 4);
    buildToolbar();
    auto* split = new QSplitter(this);
    impl_->layout->addWidget(split, 1);
    auto* list = new QWidget;
    auto* left = new QVBoxLayout(list);
    left->setContentsMargins(0, 0, 0, 0);
    left->addWidget(new QLabel(QString::fromUtf8("点云列表（Ctrl/Shift 多选；勾选控制显隐）")));
    impl_->tree = new QTreeWidget;
    impl_->tree->setObjectName("pointCloudList");
    impl_->tree->setHeaderLabels({ QString::fromUtf8("名称"), QString::fromUtf8("点数") });
    impl_->tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    impl_->tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    impl_->tree->header()->setStretchLastSection(false);
    impl_->tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    impl_->tree->setMinimumWidth(240);
    left->addWidget(impl_->tree, 1);
    auto* detail = new QWidget;
    auto* right = new QVBoxLayout(detail);
    right->setContentsMargins(0, 0, 0, 0);
    impl_->scene = new PointCloudSceneWidget;
    impl_->scene->setObjectName("pointCloudScene");
    right->addWidget(impl_->scene, 1);
    impl_->information = new QLabel;
    impl_->information->setWordWrap(true);
    impl_->information->setTextInteractionFlags(Qt::TextSelectableByMouse);
    right->addWidget(impl_->information);
    auto* group = new QGroupBox(QString::fromUtf8("处理与测量结果"));
    auto* reportLayout = new QVBoxLayout(group);
    impl_->report = new QPlainTextEdit;
    impl_->report->setReadOnly(true);
    impl_->report->setMaximumHeight(160);
    reportLayout->addWidget(impl_->report);
    right->addWidget(group);
    split->addWidget(list);
    split->addWidget(detail);
    split->setStretchFactor(1, 1);
    split->setSizes({ 280, 1000 });
    impl_->state = new QLabel(QString::fromUtf8(
        "浏览：左键旋转、滚轮缩放。选择：Ctrl 追加，Shift 减选，多边形双击或 Enter 完成，Esc 取消。"));
    impl_->state->setWordWrap(true);
    impl_->layout->addWidget(impl_->state);
    connect(impl_->tree, &QTreeWidget::currentItemChanged, this,
        [this](QTreeWidgetItem* item, QTreeWidgetItem*)
        {
            if (item)
            {
                impl_->currentId = item->data(0, Qt::UserRole).toInt();
                updateDetail(true);
            }
        });
    connect(impl_->tree, &QTreeWidget::itemChanged, this,
        [this](QTreeWidgetItem* item, int column)
        {
            if (column == 0)
                for (auto& cloud : impl_->clouds)
                    if (cloud.id == item->data(0, Qt::UserRole).toInt())
                    {
                        cloud.visible = item->checkState(0) == Qt::Checked;
                        cloud.name = item->text(0);
                        break;
                    }
            updateDetail();
        });
    impl_->tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(impl_->tree, &QTreeWidget::customContextMenuRequested, this,
        [this](const QPoint& pos)
        {
            if (impl_->busy)
                return;
            auto* item = impl_->tree->itemAt(pos);
            if (!item)
                return;
            impl_->tree->setCurrentItem(item);
            const int id = item->data(0, Qt::UserRole).toInt();
            QMenu menu;
            auto* rename = menu.addAction(QString::fromUtf8("重命名"));
            auto* color = menu.addAction(QString::fromUtf8("设置点云颜色"));
            auto* remove = menu.addAction(QString::fromUtf8("移除点云"));
            const auto chosen = menu.exec(impl_->tree->viewport()->mapToGlobal(pos));
            if (chosen == rename)
            {
                for (int i = 0; i < impl_->tree->topLevelItemCount(); ++i)
                {
                    auto* currentItem = impl_->tree->topLevelItem(i);
                    if (currentItem->data(0, Qt::UserRole).toInt() == id)
                    {
                        impl_->tree->editItem(currentItem, 0);
                        break;
                    }
                }
            }
            else if (chosen == color)
            {
                auto current = std::find_if(impl_->clouds.begin(), impl_->clouds.end(),
                    [id](const auto& cloud) { return cloud.id == id; });
                if (current != impl_->clouds.end())
                {
                    const auto selectedColor
                        = QColorDialog::getColor(current->color, this, QString::fromUtf8("点云颜色"));
                    current = std::find_if(impl_->clouds.begin(), impl_->clouds.end(),
                        [id](const auto& cloud) { return cloud.id == id; });
                    if (selectedColor.isValid() && current != impl_->clouds.end())
                    {
                        current->color = selectedColor;
                        updateDetail();
                    }
                }
            }
            else if (chosen == remove)
            {
                impl_->clouds.erase(std::remove_if(impl_->clouds.begin(), impl_->clouds.end(),
                                        [id](const auto& c) { return c.id == id; }),
                    impl_->clouds.end());
                impl_->currentId = impl_->clouds.empty() ? -1 : impl_->clouds.back().id;
                rebuildPreviewList();
                updateDetail(true);
            }
        });
    impl_->scene->setSelectionHandler(
        [this](const QPolygon& p, Qt::KeyboardModifiers m) { selectPolygon(p, m); });
    connect(&impl_->watcher, &QFutureWatcher<CloudTaskResult>::finished, this,
        &PointCloudViewWidget::finishProcessing);
    updateDetail();
}
PointCloudViewWidget::~PointCloudViewWidget()
{
    impl_->closing = true;
    impl_->cancellation.request();
    disconnect(&impl_->watcher, nullptr, this, nullptr);
    if (impl_->watcher.isRunning())
        impl_->watcher.waitForFinished();
    delete impl_->scene;
    impl_->scene = nullptr;
}
void PointCloudViewWidget::buildToolbar()
{
    impl_->toolbar = new QToolBar;
    impl_->toolbar->setObjectName("pointCloudToolbar");
    impl_->layout->addWidget(impl_->toolbar);
    const auto action = [&](const QString& text, const char* name, auto handler)
    {
        auto* a = impl_->toolbar->addAction(text);
        a->setObjectName(name);
        connect(a, &QAction::triggered, this, handler);
    };
    action(QString::fromUtf8("加载"), "cloudLoad", [this] { loadPointCloud(); });
    action(QString::fromUtf8("保存点云"), "cloudExport", [this] { exportPointCloud(); });
    impl_->toolbar->addSeparator();
    impl_->tool = new QComboBox;
    impl_->tool->setObjectName("cloudSelectionTool");
    impl_->tool->addItems({ QString::fromUtf8("浏览"), QString::fromUtf8("矩形选择"),
        QString::fromUtf8("椭圆选择"), QString::fromUtf8("套索选择"), QString::fromUtf8("多边形选择") });
    impl_->toolbar->addWidget(impl_->tool);
    connect(impl_->tool, qOverload<int>(&QComboBox::currentIndexChanged), this,
        [this](int i)
        {
            if (impl_->scene)
                impl_->scene->setSelectionTool(static_cast<CloudSelectionTool>(i));
        });
    action(QString::fromUtf8("全选"), "cloudSelectAll", [this] { editSelection(0); });
    action(QString::fromUtf8("取消"), "cloudSelectNone", [this] { editSelection(1); });
    action(QString::fromUtf8("反选"), "cloudInvertSelection", [this] { editSelection(2); });
    auto* edit = new QToolButton;
    edit->setText(QString::fromUtf8("选区编辑"));
    edit->setPopupMode(QToolButton::InstantPopup);
    auto* editMenu = new QMenu(edit);
    edit->setMenu(editMenu);
    impl_->toolbar->addWidget(edit);
    const std::array<QString, 3> names = { QString::fromUtf8("删除选中点"), QString::fromUtf8("仅保留选中点"),
        QString::fromUtf8("提取选中为新点云") };
    for (int i = 0; i < 3; ++i)
    {
        auto* a = editMenu->addAction(names[i]);
        a->setObjectName(QString("cloudEdit%1").arg(i));
        connect(a, &QAction::triggered, this, [this, i] { editSelection(3 + i); });
    }
    action(QString::fromUtf8("旋转中心"), "cloudFocus",
        [this]
        {
            auto* cloud = impl_->current();
            if (!cloud)
                return;
            Eigen::Vector3d center = Eigen::Vector3d::Zero();
            std::size_t count = 0;
            const bool selected = selectedPointCount() > 0;
            for (std::size_t i = 0; i < cloud->points->size(); ++i)
                if (!selected || cloud->selection[i])
                {
                    center += (*cloud->points)[i];
                    ++count;
                }
            if (count)
                impl_->scene->focus(center / static_cast<double>(count));
        });
    auto* operations = new QToolBar;
    operations->setObjectName("pointCloudProcessingToolbar");
    impl_->layout->addWidget(operations);
    const auto menu = [&](const QString& title, const std::vector<std::pair<QString, QString>>& entries)
    {
        auto* button = new QToolButton;
        button->setText(title);
        button->setPopupMode(QToolButton::InstantPopup);
        auto* popup = new QMenu(button);
        button->setMenu(popup);
        operations->addWidget(button);
        for (const auto& entry : entries)
        {
            auto* a = popup->addAction(entry.second);
            a->setObjectName("cloud_" + entry.first);
            connect(a, &QAction::triggered, this, [this, op = entry.first] { processCloud(op); });
        }
    };
    menu(QString::fromUtf8("滤波"),
        { { "finite", QString::fromUtf8("移除无效点") }, { "voxel", QString::fromUtf8("体素下采样") },
            { "sor", QString::fromUtf8("统计离群点滤波") }, { "pass", QString::fromUtf8("直通滤波") } });
    auto* merge = operations->addAction(QString::fromUtf8("合并点云"));
    merge->setObjectName("cloudMerge");
    connect(merge, &QAction::triggered, this, &PointCloudViewWidget::mergeClouds);
    menu(QString::fromUtf8("特征拟合"),
        { { "plane", QString::fromUtf8("平面拟合 / 两平面夹角") },
            { "sphere", QString::fromUtf8("球面拟合") }, { "cylinder", QString::fromUtf8("圆柱拟合") },
            { "doubleSphere", QString::fromUtf8("双球拟合") },
            { "quadric", QString::fromUtf8("二次曲面拟合") },
            { "quartic", QString::fromUtf8("四次曲面拟合") } });
    menu(QString::fromUtf8("缺陷测量"),
        { { "planeDefect", QString::fromUtf8("平面缺陷测量") },
            { "cylinderDefect", QString::fromUtf8("圆柱缺陷测量") } });
    connect(operations->addAction(QString::fromUtf8("误差分析")), &QAction::triggered, this,
        &PointCloudViewWidget::showErrorAnalysis);
    connect(operations->addAction(QString::fromUtf8("导出误差 CSV")), &QAction::triggered, this,
        &PointCloudViewWidget::exportErrors);
    auto* cancel = operations->addAction(QString::fromUtf8("停止处理"));
    cancel->setObjectName("cloudCancelProcessing");
    connect(cancel, &QAction::triggered, this, [this] { impl_->cancellation.request(); });
    operations->addSeparator();
    operations->addWidget(new QLabel(QString::fromUtf8("点大小 ")));
    auto* size = new QSpinBox;
    size->setRange(1, 10);
    size->setValue(2);
    operations->addWidget(size);
    connect(size, qOverload<int>(&QSpinBox::valueChanged), this,
        [this](int v) { impl_->scene->setPointSize(v); });
    operations->addWidget(new QLabel(QString::fromUtf8("误差色阶 ±")));
    impl_->errorRange = new QDoubleSpinBox;
    impl_->errorRange->setDecimals(4);
    impl_->errorRange->setRange(.0001, 1e6);
    impl_->errorRange->setValue(1);
    impl_->errorRange->setSuffix(" mm");
    operations->addWidget(impl_->errorRange);
    connect(impl_->errorRange, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
        [this](double) { updateDetail(); });
}

void PointCloudViewWidget::addPointCloud(CloudPoints points, const QString& name, const QString& source)
{
    points.erase(
        std::remove_if(points.begin(), points.end(), [](const auto& point) { return !point.allFinite(); }),
        points.end());
    if (points.empty())
        return;
    CloudEntry entry;
    entry.id = impl_->nextId++;
    entry.name = name;
    entry.source = source;
    entry.points = std::make_shared<const CloudPoints>(std::move(points));
    entry.selection.resize(entry.points->size(), false);
    impl_->currentId = entry.id;
    impl_->clouds.push_back(std::move(entry));
    rebuildPreviewList();
    updateDetail(true);
}
void PointCloudViewWidget::setPoints(const CloudPoints& points)
{
    addPointCloud(
        points, QString::fromUtf8("重建点云 %1").arg(impl_->nextId), QString::fromUtf8("软件重建结果"));
}
void PointCloudViewWidget::clear()
{
    if (impl_->busy)
        impl_->cancellation.request();
    impl_->clouds.clear();
    impl_->currentId = -1;
    rebuildPreviewList();
    updateDetail();
}
const CloudPoints& PointCloudViewWidget::currentPoints() const
{
    static const CloudPoints empty;
    const auto* cloud = impl_->current();
    return cloud ? *cloud->points : empty;
}
std::size_t PointCloudViewWidget::selectedPointCount() const
{
    const auto* cloud = impl_->current();
    return cloud ? std::count(cloud->selection.begin(), cloud->selection.end(), true) : 0;
}
bool PointCloudViewWidget::isProcessing() const { return impl_->busy; }
void PointCloudViewWidget::selectIndices(const std::vector<std::size_t>& indices)
{
    auto* cloud = impl_->current();
    if (!cloud || impl_->busy)
        return;
    std::fill(cloud->selection.begin(), cloud->selection.end(), false);
    for (auto i : indices)
        if (i < cloud->selection.size())
            cloud->selection[i] = true;
    updateDetail();
}
void PointCloudViewWidget::selectPolygon(const QPolygon& polygon, Qt::KeyboardModifiers modifiers)
{
    auto* cloud = impl_->current();
    if (!cloud || !cloud->visible || impl_->busy)
        return;
    if (!(modifiers & (Qt::ControlModifier | Qt::ShiftModifier)))
        std::fill(cloud->selection.begin(), cloud->selection.end(), false);
    const auto matrix = impl_->scene->viewProjection();
    const QSize viewport = impl_->scene->viewportSize();
    for (std::size_t i = 0; i < cloud->points->size(); ++i)
    {
        const auto& p = (*cloud->points)[i];
        const Eigen::Vector4d projected = matrix * Eigen::Vector4d(p.x(), p.y(), p.z(), 1);
        if (projected.w() <= 1e-12)
            continue;
        const Eigen::Vector3d ndc = projected.head<3>() / projected.w();
        if (ndc.z() < -1 || ndc.z() > 1 || std::abs(ndc.x()) > 1 || std::abs(ndc.y()) > 1)
            continue;
        if (polygon.containsPoint(
                QPoint(int((ndc.x() + 1) * viewport.width() / 2), int((1 - ndc.y()) * viewport.height() / 2)),
                Qt::OddEvenFill))
            cloud->selection[i] = !(modifiers & Qt::ShiftModifier);
    }
    updateDetail();
}
void PointCloudViewWidget::editSelection(int operation)
{
    auto* cloud = impl_->current();
    if (!cloud || impl_->busy)
        return;
    if (operation < 3)
    {
        for (std::size_t i = 0; i < cloud->selection.size(); ++i)
            cloud->selection[i] = operation == 0 ? true : operation == 1 ? false : !cloud->selection[i];
        updateDetail();
        return;
    }
    if (!selectedPointCount())
    {
        impl_->state->setText(QString::fromUtf8("请先选择点。"));
        return;
    }
    CloudPoints points;
    for (std::size_t i = 0; i < cloud->points->size(); ++i)
        if (cloud->selection[i] != (operation == 3))
            points.push_back((*cloud->points)[i]);
    if (points.empty())
    {
        impl_->state->setText(QString::fromUtf8("操作结果为空，原点云已保留。"));
        return;
    }
    const QString name = cloud->name, source = cloud->source;
    const auto suffix = operation == 3 ? QString::fromUtf8("_删除选中") : QString::fromUtf8("_选区");
    if (operation != 5)
        cloud->visible = false;
    addPointCloud(std::move(points), name + suffix, source);
}
void PointCloudViewWidget::rebuildPreviewList()
{
    QSignalBlocker blocker(impl_->tree);
    impl_->tree->clear();
    for (auto& cloud : impl_->clouds)
    {
        auto* item = new QTreeWidgetItem(
            impl_->tree, { cloud.name, QString::number(static_cast<qulonglong>(cloud.points->size())) });
        item->setData(0, Qt::UserRole, cloud.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        item->setCheckState(0, cloud.visible ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(0, cloud.source);
        if (cloud.id == impl_->currentId)
            impl_->tree->setCurrentItem(item);
    }
}
void PointCloudViewWidget::updateDetail(bool resetCamera)
{
    const auto* current = impl_->current();
    std::vector<CloudSceneLayer> layers;
    for (const auto& cloud : impl_->clouds)
        if (cloud.visible)
            layers.push_back({ cloud.points.get(), cloud.id == impl_->currentId ? &cloud.selection : nullptr,
                &cloud.errors, cloud.color });
    impl_->scene->setLayers(layers,
        current && current->visible ? current->fits : std::vector<CloudFitResult> { },
        impl_->errorRange->value(), resetCamera);
    if (!current)
    {
        impl_->information->setText(QString::fromUtf8("暂无点云。加载 PCD/PLY/TXT/XYZ/ASC 文件或执行重建。"));
        impl_->report->clear();
        return;
    }
    Eigen::Vector3d minimum = current->points->front(), maximum = minimum, center = Eigen::Vector3d::Zero();
    for (const auto& p : *current->points)
    {
        minimum = minimum.cwiseMin(p);
        maximum = maximum.cwiseMax(p);
        center += p / static_cast<double>(current->points->size());
    }
    impl_->information->setText(
        QString::fromUtf8("%1 | 点数 %2 | 选中 %3\n质心：%4 mm | 尺寸：%5 mm\n范围：%6 ~ "
                          "%7\n来源：%8\n误差：蓝负、绿零、红正；橙色选中点；青色拟合模型。")
            .arg(current->name)
            .arg(static_cast<qulonglong>(current->points->size()))
            .arg(static_cast<qulonglong>(selectedPointCount()))
            .arg(coordinate(center))
            .arg(coordinate(maximum - minimum))
            .arg(coordinate(minimum))
            .arg(coordinate(maximum))
            .arg(current->source));
    impl_->report->setPlainText(current->report);
}

void PointCloudViewWidget::loadPointCloud()
{
    if (impl_->busy)
        return;
    const auto file = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("加载点云"), { }, QString::fromUtf8("点云 (*.pcd *.ply *.txt *.xyz *.asc)"));
    if (file.isEmpty())
        return;
    QDialog options(this);
    options.setWindowTitle(QString::fromUtf8("点云导入选项"));
    auto* layout = new QVBoxLayout(&options);
    auto* form = new QFormLayout;
    auto* units = new QComboBox;
    units->addItems({ QString::fromUtf8("毫米"), QString::fromUtf8("厘米"), QString::fromUtf8("分米"),
        QString::fromUtf8("米") });
    form->addRow(QString::fromUtf8("源文件单位"), units);
    auto* sampling = new QComboBox;
    sampling->addItems({ "100%", "75%", "50%", "25%", "10%" });
    form->addRow(QString::fromUtf8("导入采样率"), sampling);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &options, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &options, &QDialog::reject);
    if (options.exec() != QDialog::Accepted)
        return;
    try
    {
        auto points = PointCloudService { }.load(file.toStdString());
        const double scale = std::pow(10.0, units->currentIndex()),
                     rate = std::array<double, 5> { 1, .75, .5, .25, .1 }[sampling->currentIndex()];
        CloudPoints output;
        const auto count = std::max<std::size_t>(1, points.size() * rate);
        for (std::size_t i = 0; i < count; ++i)
            output.push_back(points[i * points.size() / count] * scale);
        addPointCloud(std::move(output), QFileInfo(file).completeBaseName(), file);
    }
    catch (const std::exception& ex)
    {
        QMessageBox::warning(this, QString::fromUtf8("点云加载失败"), QString::fromUtf8(ex.what()));
    }
}
void PointCloudViewWidget::exportPointCloud()
{
    auto* cloud = impl_->current();
    if (!cloud || impl_->busy)
        return;
    const auto points = cloud->points;
    const QString name = cloud->name;
    QString filter;
    QString file = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("保存当前点云"), name, "PCD (*.pcd);;PLY (*.ply);;TXT (*.txt)", &filter);
    if (file.isEmpty())
        return;
    if (QFileInfo(file).suffix().isEmpty())
        file += filter.startsWith("PCD") ? ".pcd" : filter.startsWith("PLY") ? ".ply" : ".txt";
    try
    {
        QTemporaryDir temp;
        if (!temp.isValid())
            throw std::runtime_error("无法创建临时输出目录。");
        const auto suffix = QFileInfo(file).suffix().toLower();
        const QString staging = temp.filePath("cloud." + suffix);
        PointCloudService service;
        if (suffix == "pcd")
            service.savePcd(staging.toStdString(), *points, false);
        else if (suffix == "ply")
            service.savePly(staging.toStdString(), *points, false);
        else if (suffix == "txt")
            service.saveTxt(staging.toStdString(), *points, false);
        else
            throw std::runtime_error("请选择 PCD、PLY 或 TXT 格式。");
        QFile input(staging);
        QSaveFile output(file);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
            throw std::runtime_error("无法打开点云输出文件。");
        while (!input.atEnd())
        {
            const QByteArray bytes = input.read(1024 * 1024);
            if (bytes.isEmpty() || output.write(bytes) != bytes.size())
                throw std::runtime_error("点云写入失败。");
        }
        if (!output.commit())
            throw std::runtime_error("点云提交保存失败。");
        impl_->state->setText(QString::fromUtf8("已保存：%1").arg(file));
    }
    catch (const std::exception& ex)
    {
        QMessageBox::warning(this, QString::fromUtf8("点云保存失败"), QString::fromUtf8(ex.what()));
    }
}

void PointCloudViewWidget::processCloud(const QString& operation)
{
    auto* cloud = impl_->current();
    if (!cloud || impl_->busy)
    {
        impl_->state->setText(QString::fromUtf8("请先加载或选择点云。"));
        return;
    }
    const int sourceId = cloud->id;
    double value = .5, second = 1, standard = 0, depth = 1, connection = 0;
    int neighbors = 50, axis = 2, minimumPoints = 3;
    const bool defect = operation.endsWith("Defect");
    const bool dialogNeeded = operation == "voxel" || operation == "sor" || operation == "pass"
        || operation == "sphere" || operation == "cylinder" || operation == "doubleSphere" || defect;
    if (dialogNeeded)
    {
        QDialog dialog(this);
        dialog.setWindowTitle(QString::fromUtf8("点云处理参数"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout;
        layout->addLayout(form);
        QDoubleSpinBox *a = nullptr, *b = nullptr, *d = nullptr, *c = nullptr;
        QSpinBox* integer = nullptr;
        QComboBox* axes = nullptr;
        if (operation == "voxel")
            a = number(form, QString::fromUtf8("体素尺寸 (mm)"), .5, .0001, 1e6);
        else if (operation == "sor")
        {
            integer = new QSpinBox;
            integer->setRange(1, 200);
            integer->setValue(50);
            form->addRow(QString::fromUtf8("邻居数"), integer);
            b = number(form, QString::fromUtf8("标准差倍数"), 1, .01, 100);
        }
        else if (operation == "pass")
        {
            axes = new QComboBox;
            axes->addItems({ "X", "Y", "Z" });
            axes->setCurrentIndex(2);
            form->addRow(QString::fromUtf8("滤波轴"), axes);
            a = number(form, QString::fromUtf8("下限 (mm)"), 0, -1e9, 1e9);
            b = number(form, QString::fromUtf8("上限 (mm)"), 2000, -1e9, 1e9);
        }
        else if (defect)
        {
            d = number(form, QString::fromUtf8("最小缺陷深度 (mm)"), 1, .0001, 1e6);
            c = number(form, QString::fromUtf8("连通距离 (mm，0 自动)"), 0, 0, 1e6);
            integer = new QSpinBox;
            integer->setRange(1, 100000);
            integer->setValue(3);
            form->addRow(QString::fromUtf8("最小缺陷点数"), integer);
            layout->addWidget(
                new QLabel(QString::fromUtf8("相对拟合参考面测量；面积为局部投影包围矩形估计。")));
        }
        else
            a = number(form, QString::fromUtf8("标准直径 (mm，0 不对比)"), 0, 0, 1e9);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        if (a)
            value = a->value();
        if (b)
            second = b->value();
        if (axes)
            axis = axes->currentIndex();
        if (d)
            depth = d->value();
        if (c)
            connection = c->value();
        if (integer)
        {
            if (defect)
                minimumPoints = integer->value();
            else
                neighbors = integer->value();
        }
        if (operation == "sphere" || operation == "cylinder" || operation == "doubleSphere")
            standard = value;
    }
    // Modal dialogs process events: reconstruction may have rebuilt the cloud list.
    auto source = std::find_if(impl_->clouds.begin(), impl_->clouds.end(),
        [sourceId](const auto& entry) { return entry.id == sourceId; });
    if (source == impl_->clouds.end() || impl_->busy)
        return;
    cloud = &*source;
    auto input = cloud->points;
    if (std::any_of(cloud->selection.begin(), cloud->selection.end(), [](bool selected) { return selected; }))
    {
        auto selected = std::make_shared<CloudPoints>();
        for (std::size_t i = 0; i < input->size(); ++i)
            if (cloud->selection[i])
                selected->push_back((*input)[i]);
        input = selected;
    }
    std::shared_ptr<const CloudPoints> secondPlane;
    if (operation == "plane")
    {
        const auto ids = impl_->selectedIds();
        if (ids.size() == 2)
            for (const auto& entry : impl_->clouds)
                if (entry.id != sourceId && std::find(ids.begin(), ids.end(), entry.id) != ids.end())
                    secondPlane = entry.points;
    }
    impl_->cancellation = CancellationToken { };
    const auto cancellation = impl_->cancellation;
    impl_->busy = true;
    impl_->toolbar->setEnabled(false);
    impl_->tree->setEnabled(false);
    impl_->scene->setEnabled(false);
    impl_->state->setText(QString::fromUtf8("正在后台处理点云…"));
    impl_->watcher.setFuture(QtConcurrent::run(
        [input, secondPlane, sourceId, operation, value, second, standard, neighbors, axis, depth, connection,
            minimumPoints, defect, cancellation]
        {
            CloudTaskResult task;
            task.sourceId = sourceId;
            try
            {
                PointCloudProcessingService service;
                CloudOutput out;
                out.suffix = operation;
                cancellation.check();
                if (operation == "finite")
                {
                    out.points = service.filterFinite(*input);
                    out.suffix = QString::fromUtf8("有效点");
                }
                else if (operation == "voxel")
                {
                    out.points = service.voxelFilter(*input, value, cancellation);
                    out.suffix = QString::fromUtf8("体素滤波");
                }
                else if (operation == "sor")
                {
                    out.points = service.statisticalFilter(*input, neighbors, second, cancellation);
                    out.suffix = QString::fromUtf8("统计滤波");
                }
                else if (operation == "pass")
                {
                    out.points = service.passThrough(*input, axis, value, second);
                    out.suffix = QString::fromUtf8("直通滤波");
                }
                else
                {
                    const CloudFitKind kind = operation == "plane" || operation == "planeDefect"
                        ? CloudFitKind::Plane
                        : operation == "sphere"  ? CloudFitKind::Sphere
                        : operation == "quadric" ? CloudFitKind::Quadric
                        : operation == "quartic" ? CloudFitKind::Quartic
                                                 : CloudFitKind::Cylinder;
                    out.points = *input;
                    out.fits = operation == "doubleSphere"
                        ? service.fitDoubleSphere(*input, standard, cancellation)
                        : std::vector<CloudFitResult> { service.fit(*input, kind, standard, cancellation) };
                    for (const auto& fit : out.fits)
                        out.report += fitReport(fit) + "\n\n";
                    out.suffix = operation == "doubleSphere" ? QString::fromUtf8("双球误差")
                                                             : fitName(kind) + QString::fromUtf8("误差");
                    for (const auto& p : *input)
                    {
                        double error = PointCloudProcessingService::signedError(p, out.fits[0]);
                        if (out.fits.size() == 2)
                        {
                            const double other = PointCloudProcessingService::signedError(p, out.fits[1]);
                            if (std::abs(other) < std::abs(error))
                                error = other;
                        }
                        out.errors.push_back(error);
                    }
                    if (out.fits.size() == 2)
                        out.report += QString::fromUtf8("双球球心距离：%1 mm\n")
                                          .arg((out.fits[0].center - out.fits[1].center).norm(), 0, 'f', 5);
                    if (secondPlane)
                    {
                        const auto other = service.fit(*secondPlane, CloudFitKind::Plane, 0, cancellation);
                        out.report
                            += QString::fromUtf8("第二平面：\n%1\n两平面夹角：%2°")
                                   .arg(fitReport(other))
                                   .arg(std::acos(std::clamp(
                                            std::abs(other.direction.dot(out.fits[0].direction)), 0.0, 1.0))
                                           * 180 / 3.141592653589793,
                                       0, 'f', 5);
                    }
                    if (defect)
                    {
                        const double distance = connection > 0
                            ? connection
                            : service.estimateSpacing(*input, cancellation) * 2.5;
                        if (distance <= 0)
                            throw std::runtime_error("不能自动估计连通距离，请手动指定。");
                        const auto defects = service.measureDefects(
                            *input, out.fits[0], depth, distance, minimumPoints, cancellation);
                        out.report += QString::fromUtf8(
                            "\n连通缺陷数：%1；连通距离：%2 mm\n面积为投影包围矩形估计，非真实表面积。\n")
                                          .arg(defects.size())
                                          .arg(distance);
                        for (std::size_t i = 0; i < defects.size(); ++i)
                        {
                            const auto& item = defects[i];
                            out.report += QString::fromUtf8(
                                "%1：%2，%3 点，深度 %4 mm，长×宽 %5×%6 mm，估计面积 %7 mm²\n")
                                              .arg(i + 1)
                                              .arg(item.depression ? QString::fromUtf8("凹陷")
                                                                   : QString::fromUtf8("凸起"))
                                              .arg(item.indices.size())
                                              .arg(item.maximumDepth, 0, 'f', 4)
                                              .arg(item.length, 0, 'f', 4)
                                              .arg(item.width, 0, 'f', 4)
                                              .arg(item.projectedArea, 0, 'f', 4);
                        }
                    }
                }
                cancellation.check();
                if (out.points.empty())
                    throw std::runtime_error("处理后没有有效点，原点云已保留。");
                if (out.report.isEmpty())
                    out.report = QString::fromUtf8("输入：%1 点\n输出：%2 点\n处理：%3")
                                     .arg(input->size())
                                     .arg(out.points.size())
                                     .arg(out.suffix);
                task.outputs.push_back(std::move(out));
            }
            catch (const std::exception& ex)
            {
                task.error = QString::fromUtf8(ex.what());
            }
            return task;
        }));
}

void PointCloudViewWidget::finishProcessing()
{
    if (impl_->closing)
        return;
    impl_->busy = false;
    impl_->toolbar->setEnabled(true);
    impl_->tree->setEnabled(true);
    impl_->scene->setEnabled(true);
    auto result = impl_->watcher.result();
    if (!result.error.isEmpty())
    {
        impl_->state->setText(result.error);
        Logger::instance().warning("PointCloud", result.error.toStdString());
        emit processingFinished();
        return;
    }
    auto source = std::find_if(
        impl_->clouds.begin(), impl_->clouds.end(), [&](const auto& c) { return c.id == result.sourceId; });
    if (source == impl_->clouds.end())
    {
        emit processingFinished();
        return;
    }
    const QString name = source->name, path = source->source;
    source->visible = false;
    for (auto& out : result.outputs)
    {
        addPointCloud(std::move(out.points), name + "_" + out.suffix, path);
        auto* cloud = impl_->current();
        if (cloud)
        {
            cloud->report = std::move(out.report);
            cloud->errors = std::move(out.errors);
            cloud->fits = std::move(out.fits);
        }
    }
    updateDetail(true);
    impl_->state->setText(QString::fromUtf8("处理完成，原始点云已保留在列表中。"));
    emit processingFinished();
}
void PointCloudViewWidget::mergeClouds()
{
    if (impl_->busy)
        return;
    const auto ids = impl_->selectedIds();
    if (ids.size() < 2)
    {
        impl_->state->setText(QString::fromUtf8("请在点云列表中 Ctrl/Shift 选择至少两个点云。"));
        return;
    }
    CloudPoints points;
    for (auto& cloud : impl_->clouds)
        if (std::find(ids.begin(), ids.end(), cloud.id) != ids.end())
        {
            points.insert(points.end(), cloud.points->begin(), cloud.points->end());
            cloud.visible = false;
        }
    addPointCloud(std::move(points), QString::fromUtf8("合并点云 %1").arg(impl_->nextId),
        QString::fromUtf8("点云列表合并结果"));
}
void PointCloudViewWidget::exportErrors()
{
    const auto* cloud = impl_->current();
    if (!cloud || cloud->errors.size() != cloud->points->size())
    {
        impl_->state->setText(QString::fromUtf8("请先执行拟合或缺陷测量。"));
        return;
    }
    const auto points = cloud->points;
    const auto errors = cloud->errors;
    const QString name = cloud->name;
    QString file
        = QFileDialog::getSaveFileName(this, QString::fromUtf8("导出逐点误差"), name + ".csv", "CSV (*.csv)");
    if (file.isEmpty())
        return;
    if (QFileInfo(file).suffix().isEmpty())
        file += ".csv";
    QSaveFile output(file);
    if (!output.open(QIODevice::WriteOnly))
    {
        impl_->state->setText(output.errorString());
        return;
    }
    QTextStream stream(&output);
    stream.setCodec("UTF-8");
    stream.setRealNumberPrecision(17);
    stream << "index,x_mm,y_mm,z_mm,signed_error_mm\n";
    for (std::size_t i = 0; i < points->size(); ++i)
    {
        const auto& p = (*points)[i];
        stream << i + 1 << ',' << p.x() << ',' << p.y() << ',' << p.z() << ',' << errors[i] << '\n';
    }
    stream.flush();
    if (stream.status() != QTextStream::Ok || !output.commit())
    {
        impl_->state->setText(QString::fromUtf8("误差数据保存失败。"));
        return;
    }
    impl_->state->setText(QString::fromUtf8("误差数据已保存：%1").arg(file));
}
void PointCloudViewWidget::showErrorAnalysis()
{
    const auto* cloud = impl_->current();
    if (!cloud || cloud->errors.empty())
    {
        impl_->state->setText(QString::fromUtf8("请先执行拟合或缺陷测量。"));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QString::fromUtf8("误差分析 — %1").arg(cloud->name));
    dialog.resize(760, 560);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new ErrorHistogram(cloud->errors));
    auto* report = new QPlainTextEdit(cloud->report);
    report->setReadOnly(true);
    layout->addWidget(report);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

} // namespace htmsr::app
