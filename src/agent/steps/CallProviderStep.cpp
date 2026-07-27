#include "CallProviderStep.h"
#include "agent/AgentEvent.h"
#include "log/Logger.h"

namespace ea::agent {

namespace {

const char* role_str(Role r) {
    switch (r) {
    case Role::System:    return "system";
    case Role::User:      return "user";
    case Role::Assistant: return "assistant";
    case Role::Tool:      return "tool";
    }
    return "unknown";
}

void emit_llm_request(TurnContext& ctx, const std::vector<Message>& messages) {
    if (!ctx.emit_fn) return;

    AgentEvent event;
    event.type = AgentEventType::LLMRequest;
    event.iteration = ctx.iteration;
    event.agent_id = ctx.agent_id;
    event.messages_count = static_cast<int>(messages.size());
    event.model = ctx.model;

    // Serialize messages as [{role, content}, ...]
    json msgs_array = json::array();
    for (const auto& msg : messages) {
        msgs_array.push_back(json{{"role", role_str(msg.role)}, {"content", msg.content}});
    }
    event.request_messages = msgs_array.dump();

    ctx.emit_fn(event);
}

void emit_llm_response(TurnContext& ctx) {
    if (!ctx.emit_fn) return;

    AgentEvent event;
    event.type = AgentEventType::LLMResponse;
    event.iteration = ctx.iteration;
    event.agent_id = ctx.agent_id;
    event.assistant_output = ctx.response.content;
    event.usage = ctx.response.usage;
    event.model = ctx.model;
    event.tool_calls_count = static_cast<int>(ctx.response.tool_calls.size());

    ctx.emit_fn(event);
}

}  // anonymous namespace

CallProviderStep::CallProviderStep(ContextCompressor* compressor)
    : compressor_(compressor) {}

Result<void> CallProviderStep::execute(TurnContext& ctx) {
    if (!ctx.provider) {
        return Error::invalid_arg("No provider configured");
    }

    // Build full message list with system prompt
    std::vector<Message> messages;
    if (!ctx.system_prompt.empty()) {
        messages.push_back({Role::System, ctx.system_prompt, std::nullopt, std::nullopt, std::nullopt});
    }
    for (const auto& msg : ctx.messages) {
        messages.push_back(msg);
    }

    // Compress if needed
    if (compressor_) {
        auto compressed = compressor_->compress(messages);
        if (compressed.ok()) {
            messages = std::move(compressed.value());
        }
        // If compression fails, use original messages (graceful degradation)
    }

    // Emit LLMRequest BEFORE the provider call
    emit_llm_request(ctx, messages);

    ChatOptions opts;

    // Path 1: Streaming — provider supports it and callback is set
    if (ctx.stream_callback && ctx.provider->capabilities().streaming) {
        std::string accumulated_content;
        std::vector<ToolCall> accumulated_calls;

        try {
            auto result = ctx.provider->stream_chat(
                messages, ctx.tool_specs, "",
                [&](const StreamChunk& chunk) {
                    if (ctx.interrupted) throw StreamInterrupted{};
                    ctx.stream_callback(chunk);

                    if (chunk.type == StreamChunk::Type::Content) {
                        accumulated_content += chunk.data;
                    }
                    if (chunk.type == StreamChunk::Type::ToolCallEnd && chunk.tool_call) {
                        accumulated_calls.push_back(chunk.tool_call.value());
                    }
                },
                opts
            );

            if (!result.ok()) {
                EA_ERROR("LLM stream call failed: {}", result.error().message);
                return result.error();
            }
        } catch (const StreamInterrupted&) {
            return Error::timeout("stream interrupted");
        } catch (const std::exception& e) {
            return Error::net(std::string("stream error: ") + e.what());
        }

        ctx.response.content = std::move(accumulated_content);
        bool has_tool_calls = !accumulated_calls.empty();
        ctx.response.tool_calls = std::move(accumulated_calls);
        ctx.response.stop_reason = has_tool_calls ? "tool_calls" : "stop";

        emit_llm_response(ctx);

        return {};
    }

    // Path 2: Degraded streaming — callback set but provider doesn't support streaming
    if (ctx.stream_callback) {
        auto response = ctx.provider->chat(messages, ctx.tool_specs, "", opts);
        if (!response.ok()) {
            EA_ERROR("LLM call failed: {}", response.error().message);
            return response.error();
        }

        ctx.response = response.value();

        // Simulate Content chunk
        if (!ctx.response.content.empty()) {
            StreamChunk chunk;
            chunk.type = StreamChunk::Type::Content;
            chunk.data = ctx.response.content;
            ctx.stream_callback(chunk);
        }
        // Simulate ToolCallEnd chunks
        for (const auto& tc : ctx.response.tool_calls) {
            StreamChunk chunk;
            chunk.type = StreamChunk::Type::ToolCallEnd;
            chunk.tool_call = tc;
            ctx.stream_callback(chunk);
        }
        // Simulate Done
        StreamChunk done;
        done.type = StreamChunk::Type::Done;
        ctx.stream_callback(done);

        emit_llm_response(ctx);

        return {};
    }

    // Path 3: Non-streaming — original behavior
    auto response = ctx.provider->chat(messages, ctx.tool_specs, "", opts);
    if (!response.ok()) {
        EA_ERROR("LLM call failed: {}", response.error().message);
        return response.error();
    }

    ctx.response = std::move(response.value());

    emit_llm_response(ctx);

    return {};
}

}  // namespace ea::agent
