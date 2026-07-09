#pragma once

#include "app/acquisition/AcquisitionTypes.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace htmsr::app {

class OfflineImageSequenceProvider final : public IAcquisitionProvider {
public:
    /*
        函数功能：构造离线左右图像序列采集源
        输入：
            leftDirectory：左图像目录
            rightDirectory：右图像目录
            range：图像索引范围
        输出：
            无（构造后内部保存已配对的左右图像路径）
    */
    OfflineImageSequenceProvider(std::string leftDirectory, std::string rightDirectory, ImageRange range = {});

    std::string name() const override;
    bool hasNext() const override;
    FramePair next() override;
    void reset() override;

private:
    std::vector<std::string> leftPaths_;
    std::vector<std::string> rightPaths_;
    size_t index_ = 0;
};

} // namespace htmsr::app
