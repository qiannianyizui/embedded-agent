#include "MockProvider.h"

namespace ea::test {

MockProvider::MockProvider(std::string name, provider::ProviderCapabilities caps)
    : name_(std::move(name)), caps_(caps) {}

void MockProvider::enqueue(LLMResponse resp) {
    responses_.push(std::move(resp));
}

void MockProvider::enqueue_text(std::string text, std::string stop_reason) {
    LLMResponse resp;
    resp.content = std::move(text);
    resp.stop_reason = std::move(stop_reason);
    responses_.push(std::move(resp));
}

void MockProvider::enqueue_tool_calls(std::vector<ToolCall> calls) {
    LLMResponse resp;
    resp.content = "";
    resp.stop_reason = "tool_use";
    resp.tool_calls = std::move(calls);
    responses_.push(std::move(resp));
}

void MockProvider::enqueue_error(Error err) {
    responses_.push(LLMResponse{});
    // Store error for next chat call — we'll return it via custom handler
    auto err_copy = std::make_shared<Error>(std::move(err));
    set_chat_handler([err_copy](const std::vector<Message>&,
                                 const std::vector<ToolSpec>&,
                                 const std::string&, const ChatOptions&) -> Result<LLMResponse> {
        return *err_copy;
    });
}

void MockProvider::enqueue_chunks(std::vector<StreamChunk> chunks) {
    chunk_queues_.push(std::move(chunks));
}

std::string MockProvider::name() const { return name_; }

std::vector<std::string> MockProvider::list_models() const {
    return {name_ + "-model"};
}

provider::ProviderCapabilities MockProvider::capabilities() const {
    return caps_;
}

Result<LLMResponse> MockProvider::chat(
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    const ChatOptions& opts)
{
    call_count_++;
    last_messages_ = messages;
    last_specs_ = tools;
    last_options_ = opts;

    if (custom_handler_) {
        return custom_handler_(messages, tools, model, opts);
    }

    if (responses_.empty()) {
        return Error::net("no mock responses");
    }
    auto r = std::move(responses_.front());
    responses_.pop();
    return r;
}

Result<void> MockProvider::stream_chat(
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    std::function<void(const StreamChunk&)> on_chunk,
    const ChatOptions& opts)
{
    stream_call_count_++;
    last_messages_ = messages;
    last_specs_ = tools;
    last_options_ = opts;

    if (chunk_queues_.empty()) {
        return Error::net("no mock chunks");
    }
    auto chunks = std::move(chunk_queues_.front());
    chunk_queues_.pop();
    for (const auto& chunk : chunks) {
        on_chunk(chunk);
    }
    return {};
}

void MockProvider::set_chat_handler(ChatHandler handler) {
    custom_handler_ = std::move(handler);
}

}  // namespace ea::test
