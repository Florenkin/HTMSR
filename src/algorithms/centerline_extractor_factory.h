// 文件说明：
// 定义中心线提取器工厂，根据配置创建不同算法实现。

#pragma once

#include "algorithms/icenterline_extractor.h"

namespace htmsr::algorithms {

class CenterlineExtractorFactory {
public:
    static CenterlineExtractorPtr create(data_model::CenterlineMethod method);
};

}  // namespace htmsr::algorithms
