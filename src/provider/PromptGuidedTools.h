#pragma once
#include "base/Types.h"
#include <string>
#include <vector>

namespace ea::provider {

std::string inject_tools_into_prompt(
    const std::string& system_prompt,
    const std::vector<ToolSpec>& tools);

}  // namespace ea::provider
