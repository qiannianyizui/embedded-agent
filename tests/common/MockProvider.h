#pragma once
#include "core/IProvider.h"
#include <queue>
#include <functional>
#include <vector>

namespace ea::test {

class MockProvider : public IProvider {
public:
    explicit MockProvider(
        std::string name = "mock",
        provider::ProviderCapabilities caps = {true, true, false, false, false}
    );

    // --- Response queue ---
    void enqueue(LLMResponse resp);
    void enqueue_text(std::string text, std::string stop_reason = "stop");
    void enqueue_tool_calls(std::vector<ToolCall> calls);
    void enqueue_error(Error err);
    void enqueue_chunks(std::vector<StreamChunk> chunks);

    // --- Observation ---
    int call_count() const { return call_count_; }
    int stream_call_count() const { return stream_call_count_; }
    const std::vector<Message>& last_messages() const { return last_messages_; }
    const std::vector<ToolSpec>& last_specs() const { return last_specs_; }
    const ChatOptions& last_options() const { return last_options_; }

    // --- IProvider interface ---
    std::string name() const override;
    std::vector<std::string> list_models() const override;
    provider::ProviderCapabilities capabilities() const override;
    Result<LLMResponse> chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        const ChatOptions& opts = {}
    ) override;
    Result<void> stream_chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        std::function<void(const StreamChunk&)> on_chunk,
        const ChatOptions& opts = {}
    ) override;

    // --- Custom behavior ---
    using ChatHandler = std::function<Result<LLMResponse>(
        const std::vector<Message>&, const std::vector<ToolSpec>&,
        const std::string&, const ChatOptions&)>;
    void set_chat_handler(ChatHandler handler);

private:
    std::string name_;
    provider::ProviderCapabilities caps_;
    std::queue<LLMResponse> responses_;
    std::queue<std::vector<StreamChunk>> chunk_queues_;
    ChatHandler custom_handler_;
    int call_count_ = 0;
    int stream_call_count_ = 0;
    std::vector<Message> last_messages_;
    std::vector<ToolSpec> last_specs_;
    ChatOptions last_options_;
};

}  // namespace ea::test
