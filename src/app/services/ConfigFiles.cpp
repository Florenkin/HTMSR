#include "app/services/ConfigFiles.h"

#include <QCoreApplication>
#include <QDir>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

#include <cmath>

namespace htmsr::app {
namespace {
QString fullKey(const QString& section, const QString& key)
{
    return section.isEmpty() ? key : section + '/' + key;
}

QString scalar(const QVariant& value)
{
    if (value.type() == QVariant::Bool) return value.toBool() ? "true" : "false";
    if (value.type() == QVariant::Double) return QString::number(value.toDouble(), 'g', 16);
    return value.toString();
}
}

ConfigFile::ConfigFile(const QString& fileName) : fileName_(fileName)
{
    QFile file(fileName_);
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly)) {
        error_ = QString::fromUtf8("无法读取配置 %1：%2").arg(fileName_, file.errorString());
        return;
    }
    const auto bytes = file.readAll();
    text_ = QString::fromUtf8(bytes);
    if (text_.toUtf8() != bytes) {
        error_ = QString::fromUtf8("配置文件必须使用 UTF-8 编码：%1").arg(fileName_);
        return;
    }
    if (text_.startsWith(QChar(0xfeff))) text_.remove(0, 1);
    text_.replace("\r\n", "\n");
    QString section;
    int lineNumber = 0;
    for (const auto& original : text_.split('\n')) {
        ++lineNumber;
        const auto line = original.trimmed();
        if (line.isEmpty() || line.startsWith(';') || line.startsWith('#')) continue;
        if (line.startsWith('[') && line.endsWith(']')) {
            section = line.mid(1, line.size() - 2).trimmed();
            continue;
        }
        const int separator = line.indexOf('=');
        if (separator <= 0) {
            values_.clear();
            error_ = QString::fromUtf8("配置 %1 第 %2 行应为 参数=值。").arg(fileName_).arg(lineNumber);
            return;
        }
        const auto key = fullKey(section, line.left(separator).trimmed());
        if (values_.contains(key)) {
            values_.clear();
            error_ = QString::fromUtf8("配置 %1 中参数 %2 重复。").arg(fileName_, key);
            return;
        }
        values_.insert(key, line.mid(separator + 1).trimmed());
    }
}

QVariant ConfigFile::value(const QString& key, const QVariant& fallback) const
{
    if (!values_.contains(key)) return fallback;
    const auto text = values_.value(key);
    bool valid = true;
    QVariant result;
    switch (fallback.type()) {
    case QVariant::Bool: {
        const auto lower = text.toLower();
        valid = lower == "true" || lower == "false" || lower == "1" || lower == "0" || lower == "on" || lower == "off";
        result = lower == "true" || lower == "1" || lower == "on";
        break;
    }
    case QVariant::Int:
        result = text.toInt(&valid);
        break;
    case QVariant::Double: {
        const auto number = text.toDouble(&valid);
        valid = valid && std::isfinite(number);
        result = number;
        break;
    }
    default:
        return text;
    }
    if (valid) return result;
    error_ = QString::fromUtf8("配置 %1 的参数 %2 值无效：%3，使用默认值。").arg(fileName_, key, text);
    return fallback;
}

bool ConfigFile::contains(const QString& key) const { return values_.contains(key); }
bool ConfigFile::exists() const { return QFileInfo::exists(fileName_); }
QString ConfigFile::error() const { return error_; }

bool ConfigFile::save(const QVector<ConfigEntry>& entries)
{
    // 不覆盖无法读取或语法损坏的文件。
    if (!error_.isEmpty()) return false;
    QMap<QString, ConfigEntry> pending;
    for (const auto& entry : entries) {
        const auto value = scalar(entry.value);
        if (value.contains('\n') || value.contains('\r')) {
            error_ = QString::fromUtf8("配置 %1 的参数 %2 不能包含换行。").arg(fileName_, entry.key);
            return false;
        }
        pending.insert(entry.key, entry);
    }
    QStringList lines = text_.isEmpty() ? QStringList{} : text_.split('\n');
    QString section;
    for (auto& original : lines) {
        const auto line = original.trimmed();
        if (line.startsWith('[') && line.endsWith(']')) {
            section = line.mid(1, line.size() - 2).trimmed();
        } else if (!line.startsWith(';') && !line.startsWith('#') && line.contains('=')) {
            const auto separator = line.indexOf('=');
            const auto key = fullKey(section, line.left(separator).trimmed());
            const auto found = pending.find(key);
            if (found != pending.end()) {
                original = line.left(separator).trimmed() + '=' + scalar(found->value);
                pending.erase(found);
            }
        }
    }
    // 新参数按定义顺序添加中文说明；旧参数保留原有注释。
    QString appendedSection;
    bool hasAppendedSection = false;
    for (const auto& entry : entries) {
        if (!pending.contains(entry.key)) continue;
        const int slash = entry.key.lastIndexOf('/');
        const auto group = slash < 0 ? QString() : entry.key.left(slash);
        const auto name = slash < 0 ? entry.key : entry.key.mid(slash + 1);
        if (!hasAppendedSection || group != appendedSection) {
            lines.append(QString());
            if (!group.isEmpty()) lines.append('[' + group + ']');
            appendedSection = group;
            hasAppendedSection = true;
        }
        lines.append("; " + entry.comment);
        lines.append(name + '=' + scalar(entry.value));
    }
    if (!QDir().mkpath(QFileInfo(fileName_).absolutePath())) {
        error_ = QString::fromUtf8("无法创建配置目录：%1").arg(QFileInfo(fileName_).absolutePath());
        return false;
    }
    const auto output = (lines.join('\n').trimmed() + '\n').toUtf8();
    QSaveFile file(fileName_);
    if (!file.open(QIODevice::WriteOnly) || file.write(output) != output.size() || !file.commit()) {
        error_ = QString::fromUtf8("无法保存配置 %1：%2").arg(fileName_, file.errorString());
        return false;
    }
    text_ = QString::fromUtf8(output);
    for (const auto& entry : entries) values_.insert(entry.key, scalar(entry.value));
    return true;
}

QString ConfigFiles::directory()
{
    const auto override = qEnvironmentVariable("HTMSR_CONFIG_DIR");
    if (!override.isEmpty() && QDir::isRelativePath(override)) {
        const auto absolute = QFileInfo(override).absoluteFilePath();
        qputenv("HTMSR_CONFIG_DIR", absolute.toUtf8());
        return QDir::cleanPath(absolute);
    }
    if (!override.isEmpty()) return QDir::cleanPath(QFileInfo(override).absoluteFilePath());
    QString executableDirectory;
#ifdef Q_OS_WIN
    wchar_t buffer[32768];
    const auto length = GetModuleFileNameW(nullptr, buffer, 32768);
    if (length > 0 && length < 32768)
        executableDirectory = QFileInfo(QString::fromWCharArray(buffer, static_cast<int>(length))).absolutePath();
#else
    if (QCoreApplication::instance()) executableDirectory = QCoreApplication::applicationDirPath();
#endif
    return selectDirectory(executableDirectory, QStringLiteral(HTMSR_DEFAULT_CONFIG_DIR));
}

QString ConfigFiles::selectDirectory(const QString& executableDirectory, const QString& developmentDirectory)
{
    const auto portable = QDir(executableDirectory).filePath("config");
    return QDir::cleanPath(!executableDirectory.isEmpty() && QDir(portable).exists()
        ? portable : developmentDirectory);
}

QString ConfigFiles::path(const QString& relativePath)
{
    const auto target = QDir(directory()).filePath(relativePath);
    const auto defaults = QDir(directory()).filePath("defaults/" + relativePath);
    // 只初始化缺失文件；QFile::copy 不覆盖已有手工配置。
    if (!QFileInfo::exists(target) && QFileInfo(defaults).isFile()) QFile::copy(defaults, target);
    return target;
}

QString ConfigFiles::resolvePath(const QString& value)
{
    if (value.isEmpty()) return {};
    return QDir::isAbsolutePath(value) ? QDir::cleanPath(value)
        : QDir(directory()).absoluteFilePath(value);
}

QString ConfigFiles::environmentPath(const QString& key)
{
    ConfigFile file(path("environment.ini"));
    return resolvePath(file.value(key, QString()).toString());
}

} // namespace htmsr::app
