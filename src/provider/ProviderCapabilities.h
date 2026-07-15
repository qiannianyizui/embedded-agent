#pragma once

namespace ea::provider {

struct ProviderCapabilities {
    bool native_tool_calling = true;
    bool streaming = true;
    bool vision = false;
    bool prompt_caching = false;
    bool extended_thinking = false;
};

}  // namespace ea::provider
