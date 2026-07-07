// 文件说明：
// 实现中心线提取器工厂，统一管理不同算法的实例化逻辑。

#include "algorithms/centerline_extractor_factory.h"

#include "algorithms/gray_centerline_extractor.h"
#include "algorithms/multithreshold_centerline_extractor.h"
#include "algorithms/steger_centerline_extractor.h"

namespace htmsr::algorithms {

CenterlineExtractorPtr CenterlineExtractorFactory::create(const data_model::CenterlineMethod method) {
    switch (method) {
    case data_model::CenterlineMethod::Gray:
        return std::make_unique<GrayCenterlineExtractor>();
    case data_model::CenterlineMethod::Steger:
        return std::make_unique<StegerCenterlineExtractor>();
    case data_model::CenterlineMethod::MultiThres:
        return std::make_unique<MultiThresholdCenterlineExtractor>();
    }
    return std::make_unique<GrayCenterlineExtractor>();
}

}  // namespace htmsr::algorithms
