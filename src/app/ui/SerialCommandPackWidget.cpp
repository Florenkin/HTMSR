#include "app/ui/SerialCommandPackWidget.h"

#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QSplitter>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace htmsr::app {
namespace {

constexpr int kMaxCommandBytes = 256;
constexpr int kMaxCommands = 100;
constexpr int kMaxWaitMs = 30000;
constexpr int kMaxTotalWaitMs = 60000;

std::runtime_error lineError(int lineNumber, const QString& message)
{
    return std::runtime_error(QString::fromUtf8("第 %1 行：%2").arg(lineNumber).arg(message).toStdString());
}

std::vector<unsigned char> parseCommandLine(QString line, int lineNumber)
{
    line.replace(',', ' ');
    line.replace(';', ' ');
    const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QStringList byteTokens = tokens;
    if (tokens.size() == 1) {
        QString compact = tokens.front();
        if (compact.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            compact = compact.mid(2);
        }
        if (compact.size() > 2 && compact.size() % 2 == 0) {
            byteTokens.clear();
            for (int i = 0; i < compact.size(); i += 2) {
                byteTokens.append(compact.mid(i, 2));
            }
        }
    }

    if (byteTokens.isEmpty() || byteTokens.size() > kMaxCommandBytes) {
        throw lineError(lineNumber, QString::fromUtf8("命令必须包含 1～256 个十六进制字节。"));
    }

    static const QRegularExpression hexByte(QStringLiteral("^[0-9A-Fa-f]{2}$"));
    std::vector<unsigned char> bytes;
    bytes.reserve(static_cast<size_t>(byteTokens.size()));
    for (QString token : byteTokens) {
        if (token.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            token = token.mid(2);
        }
        if (!hexByte.match(token).hasMatch()) {
            throw lineError(lineNumber, QString::fromUtf8("非法字节 %1；每个字节应写为两位十六进制数。").arg(token));
        }
        bytes.push_back(static_cast<unsigned char>(token.toUInt(nullptr, 16)));
    }
    return bytes;
}

int getterForSetter(unsigned char opcode)
{
    switch (opcode) {
    case 0x02: return 0x03; // 同步模式
    case 0x0A: return 0x0B; // 正向速度
    case 0x0C: return 0x0D; // 反向速度
    case 0x0E: return 0x0F; // 自动旋转角度
    case 0x10: return 0x11; // 步进角度
    case 0x16: return 0x17; // 抓图间隔
    case 0x18: return 0x19; // 连续抓图等待
    case 0x1C: return 0x1D; // 电压范围
    default: return -1;
    }
}

bool isKnownGetter(unsigned char opcode)
{
    switch (opcode) {
    case 0x03:
    case 0x0B:
    case 0x0D:
    case 0x0F:
    case 0x11:
    case 0x17:
    case 0x19:
    case 0x1D:
        return true;
    default:
        return false;
    }
}

QString formatOpcode(unsigned char opcode)
{
    return QStringLiteral("%1").arg(static_cast<int>(opcode), 2, 16, QChar('0')).toUpper();
}

void validateKnownQuery(const SerialCommandPackStep& step)
{
    const auto& bytes = step.command;
    if (!step.expectResponse || bytes.size() < 4 || bytes[0] != 0x55 ||
        bytes[1] != 0xAA || bytes[2] != 0x01) {
        return;
    }

    const unsigned char opcode = bytes[3];
    const int getter = getterForSetter(opcode);
    if (getter >= 0 && bytes.size() == 5) {
        const QString queryOpcode = formatOpcode(static_cast<unsigned char>(getter));
        const QString corrected = QStringLiteral("?55 AA 01 %1 %2").arg(queryOpcode, queryOpcode);
        throw lineError(step.lineNumber,
            QString::fromUtf8("0x%1 是设置命令，不能用五字节帧查询。对应查询命令是 %2。")
                .arg(formatOpcode(opcode), corrected));
    }
    if (isKnownGetter(opcode) && (bytes.size() != 5 || bytes[4] != opcode)) {
        const QString queryOpcode = formatOpcode(opcode);
        const QString corrected = QStringLiteral("?55 AA 01 %1 %2").arg(queryOpcode, queryOpcode);
        throw lineError(step.lineNumber,
            QString::fromUtf8("查询帧长度或校验字节不正确。请使用 %1。").arg(corrected));
    }
}

} // namespace

std::vector<SerialCommandPackStep> parseSerialCommandPack(const QString& text)
{
    const QStringList lines = text.split('\n');
    static const QRegularExpression waitLine(
        QStringLiteral("^(?:WAIT|等待)\\s+(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    std::vector<SerialCommandPackStep> steps;
    int totalWaitMs = 0;
    int commandCount = 0;
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        const int commentStart = line.indexOf('#');
        if (commentStart >= 0) {
            line = line.left(commentStart);
        }
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QStringLiteral("//"))) {
            continue;
        }

        SerialCommandPackStep step;
        step.lineNumber = i + 1;
        const auto waitMatch = waitLine.match(line);
        if (waitMatch.hasMatch()) {
            step.type = SerialCommandPackStep::Type::Wait;
            bool ok = false;
            step.waitMs = waitMatch.captured(1).toInt(&ok);
            if (!ok || step.waitMs > kMaxWaitMs || totalWaitMs > kMaxTotalWaitMs - step.waitMs) {
                throw lineError(step.lineNumber, QString::fromUtf8("单次等待不能超过 30000 ms，命令包总等待不能超过 60000 ms。"));
            }
            totalWaitMs += step.waitMs;
        } else {
            if (line.startsWith('?')) {
                step.expectResponse = true;
                line = line.mid(1).trimmed();
            }
            step.command = parseCommandLine(line, step.lineNumber);
            validateKnownQuery(step);
            if (++commandCount > kMaxCommands) {
                throw lineError(step.lineNumber, QString::fromUtf8("单个命令包最多包含 100 条命令。"));
            }
        }
        steps.push_back(std::move(step));
    }
    if (commandCount == 0) {
        throw std::runtime_error("命令包至少需要一行十六进制命令。");
    }
    return steps;
}

SerialCommandPackWidget::SerialCommandPackWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto* splitter = new QSplitter(Qt::Horizontal);
    root->addWidget(splitter);

    auto* listPage = new QWidget;
    auto* listPageLayout = new QVBoxLayout(listPage);
    listPageLayout->setContentsMargins(8, 8, 8, 8);
    auto* listHeader = new QHBoxLayout;
    listHeader->addWidget(new QLabel(QString::fromUtf8("命令包预览栏")), 1);
    auto* addButton = new QPushButton(QString::fromUtf8("新建命令包"));
    listHeader->addWidget(addButton);
    listPageLayout->addLayout(listHeader);
    auto* listContainer = new QWidget;
    listLayout_ = new QVBoxLayout(listContainer);
    listLayout_->setContentsMargins(4, 4, 4, 4);
    listLayout_->setSpacing(6);
    listScroll_ = new QScrollArea;
    listScroll_->setWidgetResizable(true);
    listScroll_->setWidget(listContainer);
    listScroll_->setMinimumWidth(390);
    listPageLayout->addWidget(listScroll_, 1);

    auto* rawCommandGroup = new QGroupBox(QString::fromUtf8("单条串口指令"));
    auto* rawCommandGroupLayout = new QVBoxLayout(rawCommandGroup);
    rawCommandGroupLayout->setContentsMargins(8, 8, 8, 8);
    rawCommandGroupLayout->setSpacing(4);
    rawCommandGroupLayout->addWidget(new QLabel(QString::fromUtf8("相机指令")));
    auto* rawCommandHost = new QWidget;
    rawCommandLayout_ = new QVBoxLayout(rawCommandHost);
    rawCommandLayout_->setContentsMargins(0, 0, 0, 0);
    rawCommandGroupLayout->addWidget(rawCommandHost);
    rawCommandStatusLabel_ = new QLabel(QString::fromUtf8("等待返回"));
    rawCommandStatusLabel_->setWordWrap(true);
    rawCommandGroupLayout->addWidget(rawCommandStatusLabel_);
    rawCommandGroupLayout->addWidget(new QLabel(QString::fromUtf8("返回指令（最近 5 条，最新在上）")));
    responseList_ = new QListWidget;
    responseList_->setMinimumHeight(110);
    responseList_->setMaximumHeight(145);
    rawCommandGroupLayout->addWidget(responseList_);
    listPageLayout->addWidget(rawCommandGroup);

    auto* detailPage = new QWidget;
    auto* detailLayout = new QVBoxLayout(detailPage);
    detailLayout->setContentsMargins(8, 8, 8, 8);
    detailTitle_ = new QLabel(QString::fromUtf8("命令包详情"));
    detailLayout->addWidget(detailTitle_);
    auto* instructions = new QLabel(QString::fromUtf8(
        "每行一条完整的十六进制命令，按行顺序发送；# 后面的行尾注释不会发送。\n"
        "WAIT 1000 表示等待 1 秒；查询命令在行首加 ?。\n"
        "普通命令的“发送成功”仅表示写入串口，不代表设备已执行。串口号使用右侧在线工作流设置。"));
    instructions->setWordWrap(true);
    detailLayout->addWidget(instructions);
    commandEdit_ = new QTextEdit;
    commandEdit_->setFontFamily(QStringLiteral("Consolas"));
    commandEdit_->setPlaceholderText(QString::fromUtf8("55 AA 01 1A 1A    #打开激光\nWAIT 1000        #等待一秒\n55 AA 01 1B 1B    #关闭激光"));
    detailLayout->addWidget(commandEdit_, 1);
    statusLabel_ = new QLabel;
    statusLabel_->setWordWrap(true);
    detailLayout->addWidget(statusLabel_);
    auto* footer = new QHBoxLayout;
    footer->addStretch(1);
    saveButton_ = new QPushButton(QString::fromUtf8("保存"));
    footer->addWidget(saveButton_);
    deleteButton_ = new QPushButton(QString::fromUtf8("删除"));
    footer->addWidget(deleteButton_);
    detailLayout->addLayout(footer);

    splitter->addWidget(listPage);
    splitter->addWidget(detailPage);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 450, 870 });

    connect(addButton, &QPushButton::clicked, this, &SerialCommandPackWidget::createPack);
    connect(saveButton_, &QPushButton::clicked, this, [this]() { saveCurrent(); });
    connect(deleteButton_, &QPushButton::clicked, this, &SerialCommandPackWidget::deleteCurrent);
    connect(commandEdit_, &QTextEdit::textChanged, this, [this]() {
        if (commandEdit_->document()->isModified()) {
            statusLabel_->setText(QString::fromUtf8("命令内容已修改，请保存后再发送。"));
        }
    });

    loadPacks();
    selectedIndex_ = packs_.isEmpty() ? -1 : 0;
    rebuildList();
    updateDetail();
}

void SerialCommandPackWidget::loadPacks()
{
    QSettings settings(QStringLiteral("HTMSR"), QStringLiteral("HTMSR"));
    const bool initialized = settings.value(QStringLiteral("serialCommandPacksInitialized"), false).toBool();
    const int count = settings.beginReadArray(QStringLiteral("serialCommandPacks"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        const QString name = settings.value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty()) {
            packs_.append({ name, settings.value(QStringLiteral("content")).toString() });
        }
    }
    settings.endArray();
    if (packs_.isEmpty() && !initialized) {
        packs_.append({ QString::fromUtf8("激光开关测试"),
            QString::fromUtf8("55 AA 01 1A 1A    #打开激光\nWAIT 1000        #等待一秒\n55 AA 01 1B 1B    #关闭激光") });
        persistPacks();
    }
}

bool SerialCommandPackWidget::persistPacks()
{
    QSettings settings(QStringLiteral("HTMSR"), QStringLiteral("HTMSR"));
    settings.remove(QStringLiteral("serialCommandPacks"));
    settings.beginWriteArray(QStringLiteral("serialCommandPacks"), packs_.size());
    for (int i = 0; i < packs_.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), packs_.at(i).name);
        settings.setValue(QStringLiteral("content"), packs_.at(i).content);
    }
    settings.endArray();
    settings.setValue(QStringLiteral("serialCommandPacksInitialized"), true);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        QMessageBox::warning(this, QString::fromUtf8("保存命令包失败"), QString::fromUtf8("无法保存到应用设置，下次启动可能无法恢复命令包。"));
        return false;
    }
    return true;
}

void SerialCommandPackWidget::rebuildList()
{
    sendButtons_.clear();
    while (auto* item = listLayout_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    if (packs_.isEmpty()) {
        auto* emptyLabel = new QLabel(QString::fromUtf8("暂无命令包，请点击“新建命令包”。"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        listLayout_->addWidget(emptyLabel);
    }
    for (int i = 0; i < packs_.size(); ++i) {
        auto* row = new QFrame;
        row->setFrameShape(QFrame::StyledPanel);
        row->setStyleSheet(i == selectedIndex_
            ? QStringLiteral("QFrame { background: #eef5ff; border: 1px solid #8ab4e8; }")
            : QStringLiteral("QFrame { background: #ffffff; border: 1px solid #c8c8c8; }"));
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 6, 8, 6);
        auto* numberLabel = new QLabel(QString::number(i + 1));
        numberLabel->setMinimumWidth(28);
        numberLabel->setAlignment(Qt::AlignCenter);
        auto* nameButton = new QPushButton(packs_.at(i).name);
        nameButton->setFlat(true);
        nameButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        nameButton->setToolTip(QString::fromUtf8("查看或编辑命令包详情"));
        auto* renameButton = new QPushButton(QString::fromUtf8("重命名"));
        auto* sendButton = new QPushButton(QString::fromUtf8("发送命令"));
        sendButton->setEnabled(sendingAvailable_);
        sendButtons_.append(sendButton);
        rowLayout->addWidget(numberLabel);
        rowLayout->addWidget(nameButton, 1);
        rowLayout->addWidget(renameButton);
        rowLayout->addWidget(sendButton);
        listLayout_->addWidget(row);
        connect(nameButton, &QPushButton::clicked, this, [this, i]() { selectPack(i); });
        connect(renameButton, &QPushButton::clicked, this, [this, i]() { renamePack(i); });
        connect(sendButton, &QPushButton::clicked, this, [this, i]() { sendPack(i); });
    }
    listLayout_->addStretch(1);
}

void SerialCommandPackWidget::selectPack(int index)
{
    if (index == selectedIndex_ || index < 0 || index >= packs_.size() || !confirmLeaveCurrent()) {
        return;
    }
    selectedIndex_ = index;
    rebuildList();
    updateDetail();
}

bool SerialCommandPackWidget::confirmLeaveCurrent()
{
    if (!commandEdit_->document()->isModified()) {
        return true;
    }
    const auto choice = QMessageBox::question(this, QString::fromUtf8("未保存的命令"),
        QString::fromUtf8("当前命令包已修改，要先保存吗？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel) {
        return false;
    }
    return choice == QMessageBox::Discard || saveCurrent();
}

bool SerialCommandPackWidget::saveCurrent()
{
    if (selectedIndex_ < 0) {
        return false;
    }
    const QString content = commandEdit_->toPlainText();
    try {
        parseSerialCommandPack(content);
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, QString::fromUtf8("命令包格式错误"), QString::fromStdString(ex.what()));
        return false;
    }
    packs_[selectedIndex_].content = content;
    if (!persistPacks()) {
        return false;
    }
    commandEdit_->document()->setModified(false);
    statusLabel_->setText(QString::fromUtf8("已保存命令包。"));
    rebuildList();
    return true;
}

void SerialCommandPackWidget::deleteCurrent()
{
    if (!sendingAvailable_ || selectedIndex_ < 0 || selectedIndex_ >= packs_.size()) {
        return;
    }
    const QString name = packs_.at(selectedIndex_).name;
    const auto choice = QMessageBox::question(this, QString::fromUtf8("删除命令包"),
        QString::fromUtf8("确定删除“%1”吗？未保存的修改也会丢失，删除后无法恢复。").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        return;
    }

    const int deletedIndex = selectedIndex_;
    const CommandPack deletedPack = packs_.takeAt(deletedIndex);
    if (!persistPacks()) {
        packs_.insert(deletedIndex, deletedPack);
        return;
    }
    selectedIndex_ = packs_.isEmpty() ? -1 : std::min(deletedIndex, packs_.size() - 1);
    rebuildList();
    updateDetail();
    statusLabel_->setText(QString::fromUtf8("已删除命令包“%1”。").arg(name));
}

void SerialCommandPackWidget::createPack()
{
    if (!confirmLeaveCurrent()) {
        return;
    }
    int suffix = packs_.size() + 1;
    QString name;
    do {
        name = QString::fromUtf8("命令包 %1").arg(suffix++);
    } while (std::any_of(packs_.cbegin(), packs_.cend(), [&](const CommandPack& pack) { return pack.name == name; }));
    packs_.append({ name, QString() });
    selectedIndex_ = packs_.size() - 1;
    persistPacks();
    rebuildList();
    updateDetail();
}

void SerialCommandPackWidget::renamePack(int index)
{
    if (index < 0 || index >= packs_.size()) {
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QString::fromUtf8("重命名命令包"),
        QString::fromUtf8("命令包名称："), QLineEdit::Normal, packs_.at(index).name, &accepted).trimmed();
    if (!accepted) {
        return;
    }
    bool duplicate = false;
    for (int i = 0; i < packs_.size(); ++i) {
        if (i != index && packs_.at(i).name == name) {
            duplicate = true;
            break;
        }
    }
    if (name.isEmpty() || duplicate) {
        QMessageBox::warning(this, QString::fromUtf8("名称无效"), QString::fromUtf8("请输入非空且不重复的命令包名称。"));
        return;
    }
    packs_[index].name = name;
    persistPacks();
    rebuildList();
    if (index == selectedIndex_) {
        detailTitle_->setText(QString::fromUtf8("命令包详情：%1").arg(name));
    }
}

void SerialCommandPackWidget::sendPack(int index)
{
    if (!sendingAvailable_ || index < 0 || index >= packs_.size()) {
        return;
    }
    if (index == selectedIndex_ && commandEdit_->document()->isModified()) {
        QMessageBox::information(this, QString::fromUtf8("命令尚未保存"), QString::fromUtf8("请先保存详情页里的命令，再发送命令包。"));
        return;
    }
    try {
        parseSerialCommandPack(packs_.at(index).content);
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, QString::fromUtf8("无法发送命令包"), QString::fromStdString(ex.what()));
        return;
    }
    emit sendRequested(packs_.at(index).name, packs_.at(index).content);
}

void SerialCommandPackWidget::updateDetail()
{
    const bool hasSelection = selectedIndex_ >= 0 && selectedIndex_ < packs_.size();
    detailTitle_->setText(hasSelection
        ? QString::fromUtf8("命令包详情：%1").arg(packs_.at(selectedIndex_).name)
        : QString::fromUtf8("命令包详情"));
    commandEdit_->setEnabled(hasSelection);
    saveButton_->setEnabled(hasSelection);
    deleteButton_->setEnabled(hasSelection && sendingAvailable_);
    commandEdit_->setPlainText(hasSelection ? packs_.at(selectedIndex_).content : QString());
    commandEdit_->document()->setModified(false);
    statusLabel_->clear();
}

void SerialCommandPackWidget::setSendingAvailable(bool available)
{
    sendingAvailable_ = available;
    for (auto* button : sendButtons_) {
        button->setEnabled(available);
    }
    deleteButton_->setEnabled(available && selectedIndex_ >= 0 && selectedIndex_ < packs_.size());
}

void SerialCommandPackWidget::setRawGalvoCommandWidget(QWidget* widget)
{
    if (widget) {
        rawCommandLayout_->addWidget(widget);
    }
}

void SerialCommandPackWidget::setRawGalvoResponseText(const QString& text)
{
    rawCommandStatusLabel_->setText(text);
}

void SerialCommandPackWidget::appendRawGalvoResponse(const QString& responseText)
{
    if (responseText.trimmed().isEmpty()) {
        return;
    }
    responseList_->insertItem(0, responseText);
    while (responseList_->count() > 5) {
        delete responseList_->takeItem(5);
    }
}

void SerialCommandPackWidget::setStatusText(const QString& text)
{
    statusLabel_->setText(text);
}

} // namespace htmsr::app
