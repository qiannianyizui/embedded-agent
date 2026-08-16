#include "AnthropicProvider.h"
#include "net/SseParser.h"
#include "base/Error.h"
#include "spdlog/spdlog.h"

namespace ea::provider {

namespace {

std::string role_to_anthropic(Role role) {
    switch (role) {
        case Role::System:    return "system";  // Will be extracted separately
        case Role::User:      return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool:      return "user";  // Anthropic uses user role for tool results
    }
    return "user";
}

}  // anonymous namespace

AnthropicProvider::AnthropicProvider(Config config)
    : config_(std::move(config)) {}

std::vector<std::string> AnthropicProvider::list_models() const {
    return {};
}

json AnthropicProvider::build_request_body(
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    const ChatOptions& opts,
    bool stream
) const {
    json body;
    body["model"] = model.empty() ? config_.default_model : model;
    // Anthropic requires max_tokens; fall back to 8192 when unset (0).
    body["max_tokens"] = opts.max_tokens > 0 ? opts.max_tokens : 8192;
    body["stream"] = stream;

    // Extract system message and build messages array
    std::string system_content;
    json msgs = json::array();

    for (const auto& msg : messages) {
        if (msg.role == Role::System) {
            // Anthropic puts system as a top-level field
            system_content += msg.content + "\n";
            continue;
        }

        if (msg.role == Role::Tool) {
            // Tool result -> tool_result content block
            json m;
            m["role"] = "user";
            json content_arr = json::array();
            json block;
            block["type"] = "tool_result";
            block["tool_use_id"] = msg.tool_call_id.value_or("");
            if (msg.content.find("Error:") == 0) {
                block["is_error"] = true;
            }
            block["content"] = msg.content;
            content_arr.push_back(block);
            m["content"] = content_arr;
            msgs.push_back(m);
            continue;
        }

        if (msg.role == Role::Assistant) {
            json m;
            m["role"] = "assistant";

            // Build content blocks
            json content_arr = json::array();

            // Text content
            if (!msg.content.empty()) {
                json text_block;
                text_block["type"] = "text";
                text_block["text"] = msg.content;
                content_arr.push_back(text_block);
            }

            // Tool use blocks
            if (msg.tool_calls.has_value()) {
                for (const auto& tc : msg.tool_calls.value()) {
                    json tool_block;
                    tool_block["type"] = "tool_use";
                    tool_block["id"] = tc.id;
                    tool_block["name"] = tc.name;
                    tool_block["input"] = tc.arguments;
                    content_arr.push_back(tool_block);
                }
            }

            if (content_arr.empty()) {
                // Anthropic requires at least one content block
                json text_block;
                text_block["type"] = "text";
                text_block["text"] = "";
                content_arr.push_back(text_block);
            }

            m["content"] = content_arr;
            msgs.push_back(m);
            continue;
        }

        // User message
        json m;
        m["role"] = role_to_anthropic(msg.role);
        m["content"] = msg.content;
        msgs.push_back(m);
    }

    // Set system prompt
    if (!system_content.empty()) {
        body["system"] = system_content;
    }

    body["messages"] = msgs;

    // Tools (Anthropic format)
    if (!tools.empty()) {
        json tools_array = json::array();
        for (const auto& t : tools) {
            json tool_obj;
            tool_obj["name"] = t.name;
            tool_obj["description"] = t.description;
            tool_obj["input_schema"] = t.parameters;
            tools_array.push_back(tool_obj);
        }
        body["tools"] = tools_array;
    }

    return body;
}

Result<LLMResponse> AnthropicProvider::parse_response(const json& body) const {
    LLMResponse resp;

    // Check for error
    if (body.contains("error") && !body["error"].is_null()) {
        const auto& err = body["error"];
        std::string msg;
        int status = 400;
        if (err.is_object()) {
            msg = err.value("message", "Unknown Anthropic error");
            status = err.value("status_code", 400);
        } else if (err.is_string()) {
            msg = err.get<std::string>();
        } else {
            msg = err.dump();
        }
        return Error::net(msg, status);
    }

    // Parse content blocks
    if (body.contains("content") && body["content"].is_array()) {
        for (const auto& block : body["content"]) {
            std::string type = block.value("type", "");

            if (type == "text") {
                resp.content += block.value("text", "");
            } else if (type == "tool_use") {
                ToolCall call;
                call.id = block.value("id", "");
                call.name = block.value("name", "");
                call.arguments = block.value("input", json::object());
                resp.tool_calls.push_back(std::move(call));
            }
        }
    }

    // Stop reason
    resp.stop_reason = body.value("stop_reason", "");

    // Usage
    if (body.contains("usage") && body["usage"].is_object()) {
        const auto& u = body["usage"];
        resp.usage.input_tokens = u.value("input_tokens", 0);
        resp.usage.output_tokens = u.value("output_tokens", 0);
        resp.usage.cache_read_tokens = u.value("cache_read_input_tokens", 0);
        resp.usage.cache_write_tokens = u.value("cache_creation_input_tokens", 0);
    }

    return resp;
}

Result<LLMResponse> AnthropicProvider::chat(
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    const ChatOptions& opts
) {
    auto body = build_request_body(messages, tools, model, opts, false);

    net::RequestOptions req_opts;
    req_opts.timeout = config_.timeout;
    req_opts.tls = config_.tls;
    req_opts.retry = config_.retry;
    req_opts.body = body.dump();
    req_opts.headers["Content-Type"] = "application/json";
    req_opts.headers["x-api-key"] = config_.api_key;
    req_opts.headers["anthropic-version"] = config_.api_version;

    std::string url = config_.base_url;
    if (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    url += "/v1/messages";

    auto result = client_.post(url, req_opts);
    if (!result.ok()) {
        return result.error();
    }

    const auto& http_resp = result.value();
    if (http_resp.status != 200) {
        return Error::net("HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body, http_resp.status);
    }

    try {
        auto resp_json = json::parse(http_resp.body);
        return parse_response(resp_json);
    } catch (const json::exception& e) {
        return Error::parse(std::string("Failed to parse response: ") + e.what());
    }
}

Result<void> AnthropicProvider::stream_chat(
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    std::function<void(const StreamChunk&)> on_chunk,
    const ChatOptions& opts
) {
    auto body = build_request_body(messages, tools, model, opts, true);

    net::RequestOptions req_opts;
    req_opts.timeout = config_.timeout;
    req_opts.tls = config_.tls;
    req_opts.retry = config_.retry;
    req_opts.body = body.dump();
    req_opts.headers["Content-Type"] = "application/json";
    req_opts.headers["x-api-key"] = config_.api_key;
    req_opts.headers["anthropic-version"] = config_.api_version;

    std::string url = config_.base_url;
    if (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    url += "/v1/messages";

    // Accumulate tool calls and content from SSE stream
    std::vector<ToolCall> accumulating_calls;
    std::string content;
    net::SseParser parser;

    auto on_sse_event = [&](const net::SseEvent& event) {
        if (event.data == "[DONE]") {
            StreamChunk chunk;
            chunk.type = StreamChunk::Type::Done;
            on_chunk(chunk);
            return;
        }

        try {
            auto parsed = json::parse(event.data);
            std::string type = parsed.value("type", "");

            if (type == "content_block_start") {
                const auto& block = parsed.value("content_block", json::object());
                std::string block_type = block.value("type", "");

                if (block_type == "tool_use") {
                    int idx = parsed.value("index", 0);
                    while (static_cast<int>(accumulating_calls.size()) <= idx) {
                        accumulating_calls.emplace_back();
                    }
                    auto& acc = accumulating_calls[idx];
                    acc.id = block.value("id", "");
                    acc.name = block.value("name", "");

                    StreamChunk chunk;
                    chunk.type = StreamChunk::Type::ToolCallBegin;
                    chunk.tool_call = acc;
                    on_chunk(chunk);
                }
            } else if (type == "content_block_delta") {
                const auto& delta = parsed.value("delta", json::object());
                std::string delta_type = delta.value("type", "");

                if (delta_type == "text_delta") {
                    std::string text = delta.value("text", "");
                    content += text;
                    StreamChunk chunk;
                    chunk.type = StreamChunk::Type::Content;
                    chunk.data = text;
                    on_chunk(chunk);
                } else if (delta_type == "input_json_delta") {
                    std::string partial = delta.value("partial_json", "");
                    int idx = parsed.value("index", 0);
                    if (idx < static_cast<int>(accumulating_calls.size())) {
                        StreamChunk chunk;
                        chunk.type = StreamChunk::Type::ToolCallDelta;
                        chunk.data = partial;
                        on_chunk(chunk);
                    }
                }
            } else if (type == "message_delta") {
                const auto& delta = parsed.value("delta", json::object());
                std::string stop_reason = delta.value("stop_reason", "");

                if (!stop_reason.empty()) {
                    // Emit ToolCallEnd for accumulated tool calls
                    for (auto& tc : accumulating_calls) {
                        // Try to parse accumulated input as JSON
                        try {
                            if (tc.arguments.is_string()) {
                                tc.arguments = json::parse(tc.arguments.get<std::string>());
                            }
                        } catch (...) {}

                        StreamChunk chunk;
                        chunk.type = StreamChunk::Type::ToolCallEnd;
                        chunk.tool_call = tc;
                        on_chunk(chunk);
                    }
                    accumulating_calls.clear();
                }

                // Usage
                const auto& usage = parsed.value("usage", json::object());
                if (!usage.empty()) {
                    StreamChunk chunk;
                    chunk.type = StreamChunk::Type::Done;
                    Usage u;
                    u.output_tokens = usage.value("output_tokens", 0);
                    chunk.usage = u;
                    on_chunk(chunk);
                }
            }
        } catch (const json::exception& e) {
            spdlog::warn("Failed to parse Anthropic SSE chunk: {}", e.what());
        }
    };

    auto on_data = [&](const std::string& data) {
        parser.feed(data, on_sse_event);
    };

    auto result = client_.stream_post(url, on_data, req_opts);
    if (!result.ok()) {
        return result.error();
    }

    return {};
}

}  // namespace ea::provider
