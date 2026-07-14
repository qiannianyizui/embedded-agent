#pragma once
#include "core/IProvider.h"
#include "config/Config.h"
#include <memory>

namespace ea::provider {

std::unique_ptr<IProvider> create(const config::ProviderConfig& cfg);

}  // namespace ea::provider
