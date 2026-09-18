#pragma once

#include "core/Types.h"

#include <QString>

namespace htmsr::app {

class AppConfigService {
public:
    /*
        函数功能：从 config 分类配置文件中读取软件工程参数
        输入：
            无
        输出：
            返回值：项目路径、标定参数、重建参数和输出目录等配置
    */
    AppProjectConfig load() const;

    /*
        函数功能：将当前参数保存到 config，并保留中文注释
        输入：
            config：待保存的软件工程配置
        输出：
            返回值：成功为 true，失败为 false；lastError 提供错误说明
    */
    bool save(const AppProjectConfig& config) const;
    QString lastError() const;

private:
    mutable QString lastError_;
};

} // namespace htmsr::app
