#pragma once
#include "core/IProvider.h"
#include "config/Config.h"
#include <memory>

namespace ea::provider {

struct FactoryOptions {
    bool enable_fallback = false;
    bool enable_retry = true;
    int max_retries = 3;
};

std::unique_ptr<IProvider> create(const config::ProviderConfig& cfg,
                                   const FactoryOptions& opts = {});

}  // namespace ea::provider
