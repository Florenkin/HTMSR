#pragma once

#include "core/Types.h"

#include <QString>

namespace htmsr::app {

class AppConfigService {
public:
    AppProjectConfig load() const;
    void save(const AppProjectConfig& config) const;
};

} // namespace htmsr::app
