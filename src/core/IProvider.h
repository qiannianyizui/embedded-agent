#pragma once
#include "Types.h"
#include "common/base/Result.h"
#include <functional>

namespace ea {

class IProvider {
public:
    virtual ~IProvider() = default;
    virtual std::string name() const = 0;
    virtual std::vector<std::string> list_models() const = 0;
    virtual Result<LLMResponse> chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        const ChatOptions& opts = {}) = 0;
    virtual Result<void> stream_chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        std::function<void(const StreamChunk&)> on_chunk,
        const ChatOptions& opts = {}) = 0;
};

}  // namespace ea
