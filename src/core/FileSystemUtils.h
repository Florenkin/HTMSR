#pragma once

#include "core/Types.h"

#include <string>
#include <vector>

namespace htmsr {

/*
    函数功能：递归扫描目录，收集可处理的图像文件路径并按名称排序
    输入：
        directory：图像所在目录
        range：图像索引范围，默认保留全部图像
    输出：
        返回值：筛选后的图像绝对路径列表
*/
std::vector<std::string> listImageFiles(const std::string& directory, const ImageRange& range = {});

/*
    函数功能：确保输出文件的父目录存在
    输入：
        filePath：输出文件路径
    输出：
        返回值：父目录已存在或创建成功时返回 true
*/
bool ensureParentDirectory(const std::string& filePath);

} // namespace htmsr
