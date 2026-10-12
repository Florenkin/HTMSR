#pragma once

#include "app/acquisition/AcquisitionTypes.h"

namespace htmsr::app {

class MockAcquisitionProvider final : public IAcquisitionProvider {
public:
    /*
        函数功能：构造用于无相机开发阶段的模拟双目采集源
        输入：
            frameCount：需要生成的模拟帧数量
        输出：
            无（构造后内部保存待生成帧数量）
    */
    explicit MockAcquisitionProvider(int frameCount);

    std::string name() const override;
    bool hasNext() const override;
    FramePair next() override;
    void reset() override;

private:
    int frameCount_ = 0;
    int index_ = 0;
};

} // namespace htmsr::app
