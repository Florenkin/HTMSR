#include "app/ui/LogPanel.h"

#include "core/Logger.h"

#include <QDateTime>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace htmsr::app {

LogPanel::LogPanel(QWidget* parent)
    : QWidget(parent)
{
    // 日志表固定为时间、级别、模块、消息四列，方便定位算法流程问题。
    table_ = new QTableWidget(0, 4);
    table_->setHorizontalHeaderLabels({
        QString::fromUtf8("时间"),
        QString::fromUtf8("级别"),
        QString::fromUtf8("模块"),
        QString::fromUtf8("消息")
    });
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(table_);
}

void LogPanel::appendMessage(const htmsr::LogMessage& message)
{
    // 每条日志追加到表格末尾，并自动滚动到底部。
    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, 0, new QTableWidgetItem(QDateTime::currentDateTime().toString("HH:mm:ss.zzz")));
    table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(toString(message.level))));
    table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(message.module)));
    table_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(message.text)));
    table_->scrollToBottom();
}

void LogPanel::clear()
{
    table_->setRowCount(0);
}

} // namespace htmsr::app
