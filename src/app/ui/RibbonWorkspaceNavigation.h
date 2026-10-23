#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

class QTabWidget;
class SARibbonBar;
class SARibbonCategory;

namespace htmsr::app {

// 顶部 Ribbon 是唯一页面导航；中央 QTabWidget 只负责保留和切换已有工作区。
class RibbonWorkspaceNavigation final : public QObject {
    Q_OBJECT
public:
    explicit RibbonWorkspaceNavigation(SARibbonBar* ribbon, QTabWidget* workspaces,
        QObject* parent = nullptr);
    SARibbonCategory* category(const QString& objectName) const;
    void showWorkspace(int index);

signals:
    // 0=标定，1=重建；是否允许切换仍由采集面板的忙碌/会话状态决定。
    void workflowPageRequested(int index);

private:
    void onRibbonTabChanged(int index);
    void onWorkspaceChanged(int index);

    QPointer<SARibbonBar> ribbon_;
    QPointer<QTabWidget> workspaces_;
    QHash<QString, QPointer<SARibbonCategory>> categories_;
    QHash<SARibbonCategory*, int> workspaceIndices_;
    QHash<SARibbonCategory*, int> workflowIndices_;
    QHash<int, QPointer<SARibbonCategory>> primaryCategories_;
    bool synchronizing_ = false;
};

} // namespace htmsr::app
