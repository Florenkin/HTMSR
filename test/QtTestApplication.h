#pragma once

#include <QDir>
#include <QFileInfo>
#include <QtGlobal>

namespace htmsr::test {

// 独立启动测试也使用与构建版本匹配的 Qt 插件，不依赖 CTest 或旧电脑的环境变量。
inline void configureQtTestApplication(const char* executable)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    const QString applicationDirectory = QFileInfo(QString::fromLocal8Bit(executable)).absolutePath();
    const QString deployedPlatforms = QDir(applicationDirectory).filePath("platforms");
    const bool deployedOffscreen = qEnvironmentVariable("QT_QPA_PLATFORM").section(':', 0, 0) == QStringLiteral("offscreen")
        && QFileInfo::exists(QDir(deployedPlatforms).filePath(QStringLiteral(HTMSR_QT_TEST_PLUGIN)));
    const QString pluginRoot = deployedOffscreen
        ? applicationDirectory : QStringLiteral(HTMSR_QT_TEST_PLUGIN_ROOT);
    qputenv("QT_PLUGIN_PATH", QDir::toNativeSeparators(pluginRoot).toLocal8Bit());
    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QDir::toNativeSeparators(QDir(pluginRoot).filePath("platforms")).toLocal8Bit());
}

} // namespace htmsr::test
