#pragma once

#include <QMap>
#include <QString>
#include <QVariant>
#include <QVector>

namespace htmsr::app {

struct ConfigEntry {
    QString key;
    QVariant value;
    QString comment;
};

// UTF-8 INI：原子保存，并保留手工添加的注释和未知参数。
class ConfigFile {
public:
    explicit ConfigFile(const QString& fileName);
    QVariant value(const QString& key, const QVariant& fallback) const;
    bool contains(const QString& key) const;
    bool exists() const;
    bool save(const QVector<ConfigEntry>& entries);
    QString error() const;

private:
    QString fileName_;
    QString text_;
    QMap<QString, QString> values_;
    mutable QString error_;
};

class ConfigFiles {
public:
    // 测试或便携部署可通过 HTMSR_CONFIG_DIR 指定独立目录。
    static QString directory();
    static QString selectDirectory(const QString& executableDirectory, const QString& developmentDirectory);
    static QString path(const QString& relativePath);
    static QString resolvePath(const QString& value);
    static QString environmentPath(const QString& key);
};

} // namespace htmsr::app
