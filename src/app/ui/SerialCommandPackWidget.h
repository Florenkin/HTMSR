#pragma once

#include <QList>
#include <QString>
#include <QWidget>

#include <vector>

class QLabel;
class QListWidget;
class QPushButton;
class QScrollArea;
class QTextEdit;
class QVBoxLayout;

namespace htmsr::app {

struct SerialCommandPackStep {
    enum class Type { Command, Wait };
    Type type = Type::Command;
    int lineNumber = 0;
    int waitMs = 0;
    bool expectResponse = false;
    std::vector<unsigned char> command;
};

std::vector<SerialCommandPackStep> parseSerialCommandPack(const QString& text);

class SerialCommandPackWidget final : public QWidget {
    Q_OBJECT

public:
    explicit SerialCommandPackWidget(QWidget* parent = nullptr);
    void setRawGalvoCommandWidget(QWidget* widget);
    void setRawGalvoResponseText(const QString& text);
    void appendRawGalvoResponse(const QString& responseText);
    void setSendingAvailable(bool available);
    void setStatusText(const QString& text);

signals:
    void sendRequested(const QString& name, const QString& content);

private:
    struct CommandPack {
        QString name;
        QString content;
    };

    void loadPacks();
    bool persistPacks();
    void rebuildList();
    void selectPack(int index);
    bool confirmLeaveCurrent();
    bool saveCurrent();
    void deleteCurrent();
    void createPack();
    void renamePack(int index);
    void sendPack(int index);
    void updateDetail();

    QList<CommandPack> packs_;
    QList<QPushButton*> sendButtons_;
    int selectedIndex_ = -1;
    bool sendingAvailable_ = true;
    QScrollArea* listScroll_ = nullptr;
    QVBoxLayout* listLayout_ = nullptr;
    QVBoxLayout* rawCommandLayout_ = nullptr;
    QLabel* rawCommandStatusLabel_ = nullptr;
    QListWidget* responseList_ = nullptr;
    QLabel* detailTitle_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTextEdit* commandEdit_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
};

} // namespace htmsr::app
