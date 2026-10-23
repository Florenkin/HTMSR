#include "app/ui/RibbonWorkspaceNavigation.h"

#include <SARibbon.h>
#include <QScopedValueRollback>
#include <QTabBar>
#include <QTabWidget>
#include <stdexcept>

namespace htmsr::app {

/*
    函数功能：合并功能区和中央工作区导航，建立页面及工作流的对应关系
    输入：
        ribbon：尚未添加分类页的主功能区
        workspaces：按双目、点云、采集、激光线、命令排列的五个已有页面
        parent：导航对象的生命周期所属窗口
    输出：
        副作用：创建一排功能区入口并隐藏中央重复标签；不创建或清空业务页面
*/
RibbonWorkspaceNavigation::RibbonWorkspaceNavigation(SARibbonBar* ribbon, QTabWidget* workspaces,
    QObject* parent)
    : QObject(parent ? parent : ribbon), ribbon_(ribbon), workspaces_(workspaces)
{
    if (!ribbon || !workspaces || workspaces->count() != 5 || !ribbon->categoryPages().isEmpty())
        throw std::invalid_argument("Ribbon navigation requires an empty ribbon and five existing workspaces.");

    struct Entry {
        const char* title;
        const char* name;
        int workspace;
        int workflow;
        bool primary;
    };
    const Entry entries[] = {
        {"双目", "stereoCategory", 0, -1, true},
        {"标定", "acquisitionCategory", 0, 0, false},
        {"重建", "reconstructionCategory", 0, 1, false},
        {"点云", "cloudCategory", 1, -1, true},
        {"采集图像", "captureCategory", 2, -1, true},
        {"激光线", "laserCategory", 3, -1, true},
        {"命令", "commandCategory", 4, -1, true},
        {"视图", "viewCategory", -1, -1, false},
        {"帮助", "helpCategory", -1, -1, false}
    };
    for (const auto& entry : entries) {
        auto* page = ribbon->addCategoryPage(QString::fromUtf8(entry.title));
        page->setObjectName(QString::fromLatin1(entry.name));
        categories_.insert(page->objectName(), page);
        if (entry.workspace >= 0)
            workspaceIndices_.insert(page, entry.workspace);
        if (entry.workflow >= 0)
            workflowIndices_.insert(page, entry.workflow);
        if (entry.primary)
            primaryCategories_.insert(entry.workspace, page);
    }
    workspaces->tabBar()->hide();
    connect(ribbon, &SARibbonBar::currentRibbonTabChanged, this,
        &RibbonWorkspaceNavigation::onRibbonTabChanged);
    connect(workspaces, &QTabWidget::currentChanged, this,
        &RibbonWorkspaceNavigation::onWorkspaceChanged);
    showWorkspace(workspaces->currentIndex());
}

SARibbonCategory* RibbonWorkspaceNavigation::category(const QString& objectName) const
{
    return categories_.value(objectName);
}

void RibbonWorkspaceNavigation::showWorkspace(int index)
{
    if (!ribbon_ || !workspaces_ || !primaryCategories_.value(index))
        return;
    const QScopedValueRollback<bool> guard(synchronizing_, true);
    workspaces_->setCurrentIndex(index);
    ribbon_->setCurrentIndex(ribbon_->categoryIndex(primaryCategories_.value(index)));
}

void RibbonWorkspaceNavigation::onRibbonTabChanged(int index)
{
    if (synchronizing_ || !ribbon_ || !workspaces_)
        return;
    auto* page = ribbon_->categoryByIndex(index);
    const auto workspace = workspaceIndices_.constFind(page);
    if (workspace != workspaceIndices_.constEnd()) {
        // 保留“标定/重建”别名的选中状态，不让中央页的回调立即切回“双目”。
        const QScopedValueRollback<bool> guard(synchronizing_, true);
        workspaces_->setCurrentIndex(workspace.value());
    }
    const auto workflow = workflowIndices_.constFind(page);
    if (workflow != workflowIndices_.constEnd())
        emit workflowPageRequested(workflow.value());
}

void RibbonWorkspaceNavigation::onWorkspaceChanged(int index)
{
    if (!synchronizing_)
        showWorkspace(index);
}

} // namespace htmsr::app
