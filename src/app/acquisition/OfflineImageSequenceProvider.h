#pragma once

#include "app/acquisition/AcquisitionTypes.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace htmsr::app {

class OfflineImageSequenceProvider final : public IAcquisitionProvider {
public:
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
