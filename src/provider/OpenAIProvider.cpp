#include "OpenAIProvider.h"
#include "net/SseParser.h"
#include "base/Error.h"
#include "spdlog/spdlog.h"

namespace ea::provider {

namespace {

std::string role_to_string(Role role) {
    switch (role) {
        case Role::System:    return "system";
        case Role::User:      return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool:      return "tool";
    }
    return "user";
}

}  // anonymous namespace

OpenAIProvider::OpenAIProvider(Config config)
    : config_(std::move(config)) {}

std::vector<std::string> OpenAIProvider::list_models() const {
    return {};
}

json OpenAIProvider::build_request_body(
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    const ChatOptions& opts,
    bool stream
) const {
    json body;
    body["model"] = model.empty() ? config_.default_model : model;
    body["stream"] = stream;

    // Messages
    json msgs = json::array();
    for (const auto& msg : messages) {
        json m;
        m["role"] = role_to_string(msg.role);
        if (!msg.content.empty()) {
            m["content"] = msg.content;
        } else {
            m["content"] = nullptr;
        }
        if (msg.name.has_value()) {
            m["name"] = msg.name.value();
        }
        if (msg.tool_calls.has_value()) {
            json tc_array = json::array();
            for (const auto& tc : msg.tool_calls.value()) {
                json tc_obj;
                tc_obj["id"] = tc.id;
                tc_obj["type"] = "function";
                tc_obj["function"]["name"] = tc.name;
                if (tc.arguments.is_string()) {
                    tc_obj["function"]["arguments"] = tc.arguments;
                } else {
                    tc_obj["function"]["arguments"] = tc.arguments.dump();
                }
                tc_array.push_back(tc_obj);
            }
            m["tool_calls"] = tc_array;
        }
        if (msg.tool_call_id.has_value()) {
            m["tool_call_id"] = msg.tool_call_id.value();
        }
        msgs.push_back(m);
    }
    body["messages"] = msgs;

    // Tools
    if (!tools.empty()) {
        json tools_array = json::array();
        for (const auto& t : tools) {
            json tool_obj;
            tool_obj["type"] = "function";
            tool_obj["function"]["name"] = t.name;
            tool_obj["function"]["description"] = t.description;
            tool_obj["function"]["parameters"] = t.parameters;
            tools_array.push_back(tool_obj);
        }
        body["tools"] = tools_array;
    }

    // Options
    body["temperature"] = opts.temperature;
    body["max_tokens"] = opts.max_tokens;
    body["top_p"] = opts.top_p;
    if (opts.stop.has_value()) {
        body["stop"] = opts.stop.value();
    }

    return body;
}

Result<LLMResponse> OpenAIProvider::parse_response(const json& body) const {
    if (!body.contains("choices") || !body["choices"].is_array() || body["choices"].empty()) {
        return Error::parse("No choices in response");
    }

    const auto& choice = body["choices"][0];
    if (!choice.is_object()) {
        return Error::parse("Invalid choice element: expected object");
    }
    LLMResponse resp;

    // Content
    if (choice.contains("message")) {
        const auto& msg = choice["message"];
        if (msg.contains("content") && !msg["content"].is_null()) {
            resp.content = msg["content"].get<std::string>();
        }

        // Tool calls
        if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
            for (const auto& tc : msg["tool_calls"]) {
                ToolCall call;
                call.id = tc.value("id", "");
                call.name = tc["function"].value("name", "");
                if (tc["function"].contains("arguments")) {
                    const auto& args = tc["function"]["arguments"];
                    if (args.is_string()) {
                        try {
                            call.arguments = json::parse(args.get<std::string>());
                        } catch (...) {
                            call.arguments = args;
                        }
                    } else {
                        call.arguments = args;
                    }
                }
                resp.tool_calls.push_back(std::move(call));
            }
        }
    }

    // Finish reason
    resp.stop_reason = choice.value("finish_reason", "");

    // Usage
    if (body.contains("usage") && body["usage"].is_object()) {
        const auto& u = body["usage"];
        resp.usage.input_tokens = u.value("prompt_tokens", 0);
        resp.usage.output_tokens = u.value("completion_tokens", 0);
    }

    return resp;
}

Result<LLMResponse> OpenAIProvider::chat(
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
    if (!config_.api_key.empty()) {
        req_opts.headers["Authorization"] = "Bearer " + config_.api_key;
    }

    std::string url = config_.base_url;
    if (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    url += "/chat/completions";

    auto result = client_.post(url, req_opts);
    if (!result.ok()) {
        return result.error();
    }

    const auto& http_resp = result.value();
    if (http_resp.status != 200) {
        return Error::net("HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body);
    }

    try {
        auto resp_json = json::parse(http_resp.body);
        return parse_response(resp_json);
    } catch (const json::exception& e) {
        return Error::parse(std::string("Failed to parse response: ") + e.what());
    }
}

Result<void> OpenAIProvider::stream_chat(
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
    if (!config_.api_key.empty()) {
        req_opts.headers["Authorization"] = "Bearer " + config_.api_key;
    }

    std::string url = config_.base_url;
    if (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    url += "/chat/completions";

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
            if (!parsed.contains("choices") || parsed["choices"].empty()) return;

            const auto& delta = parsed["choices"][0];
            parse_sse_chunk(delta, accumulating_calls, content, on_chunk);
        } catch (const json::exception& e) {
            spdlog::warn("Failed to parse SSE chunk: {}", e.what());
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

void OpenAIProvider::parse_sse_chunk(
    const json& delta,
    std::vector<ToolCall>& accumulating_calls,
    std::string& content,
    std::function<void(const StreamChunk&)> on_chunk
) const {
    // Content delta
    if (delta.contains("delta")) {
        const auto& d = delta["delta"];
        if (d.contains("content") && !d["content"].is_null()) {
            std::string text = d["content"].get<std::string>();
            content += text;
            StreamChunk chunk;
            chunk.type = StreamChunk::Type::Content;
            chunk.data = text;
            on_chunk(chunk);
        }

        // Tool call deltas
        if (d.contains("tool_calls") && d["tool_calls"].is_array()) {
            for (const auto& tc_delta : d["tool_calls"]) {
                int idx = tc_delta.value("index", 0);

                // Ensure we have room
                while (static_cast<int>(accumulating_calls.size()) <= idx) {
                    accumulating_calls.emplace_back();
                }

                auto& acc = accumulating_calls[idx];

                if (tc_delta.contains("id") && !tc_delta["id"].is_null()) {
                    acc.id = tc_delta["id"].get<std::string>();

                    StreamChunk chunk;
                    chunk.type = StreamChunk::Type::ToolCallBegin;
                    chunk.tool_call = acc;
                    on_chunk(chunk);
                }

                if (tc_delta.contains("function")) {
                    const auto& fn = tc_delta["function"];
                    if (fn.contains("name") && !fn["name"].is_null()) {
                        acc.name += fn["name"].get<std::string>();
                    }
                    if (fn.contains("arguments") && !fn["arguments"].is_null()) {
                        std::string arg_delta = fn["arguments"].get<std::string>();
                        StreamChunk chunk;
                        chunk.type = StreamChunk::Type::ToolCallDelta;
                        chunk.data = arg_delta;
                        // Accumulate raw string; parsed into object at finish_reason
                        if (acc.arguments.is_string()) {
                            acc.arguments = acc.arguments.get<std::string>() + arg_delta;
                        } else {
                            acc.arguments = arg_delta;
                        }
                        on_chunk(chunk);
                    }
                }
            }
        }
    }

    // Finish reason
    if (delta.contains("finish_reason") && !delta["finish_reason"].is_null()) {
        // Emit ToolCallEnd for any accumulated tool calls
        for (auto& tc : accumulating_calls) {
            // Try to parse accumulated arguments as JSON
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

    // Usage (may appear in the final chunk)
    if (delta.contains("usage") && delta["usage"].is_object()) {
        StreamChunk chunk;
        chunk.type = StreamChunk::Type::Content;
        Usage u;
        u.input_tokens = delta["usage"].value("prompt_tokens", 0);
        u.output_tokens = delta["usage"].value("completion_tokens", 0);
        chunk.usage = u;
        on_chunk(chunk);
    }
}

}  // namespace ea::provider
