#pragma once

#include <QStringList>

namespace htmsr::app {

inline QStringList mainWindowCentralTabTitles()
{
    return {
        QString::fromUtf8("双目"),
        QString::fromUtf8("点云"),
        QString::fromUtf8("采集"),
        QString::fromUtf8("激光线"),
        QString::fromUtf8("命令")
    };
}

} // namespace htmsr::app
