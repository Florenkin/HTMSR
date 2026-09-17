#include "app/ui/LogPanel.h"

#include "core/Logger.h"

#include <QDateTime>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

namespace htmsr::app {

LogPanel::LogPanel(QWidget* parent)
    : QWidget(parent)
{
    // 日志表固定为时间、级别、模块、消息四列，方便定位算法流程问题。
    table_ = new QTableWidget(0, 4);
    table_->setObjectName("logTable");
    table_->setHorizontalHeaderLabels({
        QString::fromUtf8("时间"),
        QString::fromUtf8("级别"),
        QString::fromUtf8("模块"),
        QString::fromUtf8("消息")
    });
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8);
    table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    table_->setColumnWidth(0, 120);
    table_->setColumnWidth(1, 75);
    table_->setColumnWidth(2, 150);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(table_);
    setMinimumHeight(sizeHint().height());
}

QSize LogPanel::sizeHint() const
{
    return QSize(800, table_->horizontalHeader()->sizeHint().height() +
        2 * table_->verticalHeader()->defaultSectionSize() + 2 * table_->frameWidth());
}

QSize LogPanel::minimumSizeHint() const
{
    return QSize(0, sizeHint().height());
}

void LogPanel::appendMessage(const htmsr::LogMessage& message)
{
    if (message.level == LogLevel::Debug) {
        return;
    }
    // 两行只是默认可见高度，历史记录仍可滚动查看；限制内存占用，完整关键日志保存在文件中。
    constexpr int maximumRows = 2000;
    if (table_->rowCount() >= maximumRows) {
        table_->removeRow(0);
    }
    const int row = table_->rowCount();
    table_->insertRow(row);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        message.timestamp.time_since_epoch()).count();
    table_->setItem(row, 0, new QTableWidgetItem(QDateTime::fromMSecsSinceEpoch(milliseconds).toString("HH:mm:ss.zzz")));
    table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(toString(message.level))));
    table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(message.module)));
    const QString text = QString::fromStdString(message.text);
    auto* item = new QTableWidgetItem(text.simplified());
    item->setToolTip(text);
    table_->setItem(row, 3, item);
    if (!scrollPending_) {
        scrollPending_ = true;
        QTimer::singleShot(0, this, [this]() {
            scrollPending_ = false;
            table_->scrollToBottom();
        });
    }
}

void LogPanel::clear()
{
    table_->setRowCount(0);
}

} // namespace htmsr::app
