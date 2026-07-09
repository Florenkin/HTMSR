#pragma once

#include "core/Types.h"

#include <QString>

namespace htmsr::app {

class AppConfigService {
public:
    /*
        函数功能：从系统配置中读取上次保存的软件工程参数
        输入：
            无
        输出：
            返回值：项目路径、标定参数、重建参数和输出目录等配置
    */
    AppProjectConfig load() const;

    /*
        函数功能：将当前软件工程参数保存到系统配置
        输入：
            config：待保存的软件工程配置
        输出：
            无
    */
    void save(const AppProjectConfig& config) const;
};

} // namespace htmsr::app
