#include "OllamaProvider.h"
#include "base/Error.h"
#include "log/Logger.h"
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

std::string strip_trailing_slash(const std::string& url) {
    std::string result = url;
    while (!result.empty() && result.back() == '/') {
        result.pop_back();
    }
    return result;
}

}  // anonymous namespace

OllamaProvider::OllamaProvider(Config config)
    : config_(std::move(config)) {}

json OllamaProvider::build_request_body(
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

        if (msg.role == Role::Tool) {
            // Ollama tool result format: role=tool, tool_name, content
            std::string tool_name;
            if (msg.name.has_value() && !msg.name->empty()) {
                tool_name = msg.name.value();
            } else if (msg.tool_call_id.has_value() && !msg.tool_call_id->empty()) {
                tool_name = msg.tool_call_id.value();
            }
            m["tool_name"] = tool_name;
            m["content"] = msg.content;
        } else if (msg.role == Role::Assistant && msg.tool_calls.has_value()) {
            // Assistant with tool calls
            m["content"] = msg.content;
            json tc_array = json::array();
            int idx = 0;
            for (const auto& tc : msg.tool_calls.value()) {
                json tc_obj;
                tc_obj["type"] = "function";
                tc_obj["function"]["index"] = idx;
                tc_obj["function"]["name"] = tc.name;
                if (tc.arguments.is_string()) {
                    tc_obj["function"]["arguments"] = tc.arguments;
                } else {
                    tc_obj["function"]["arguments"] = tc.arguments.dump();
                }
                tc_array.push_back(tc_obj);
                ++idx;
            }
            m["tool_calls"] = tc_array;
        } else {
            // System, User, or Assistant without tool calls
            if (!msg.content.empty()) {
                m["content"] = msg.content;
            } else {
                m["content"] = nullptr;
            }
        }

        msgs.push_back(m);
    }
    body["messages"] = msgs;

    // Tools (same format as OpenAI)
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

    // Ollama options (nested under "options" key)
    json options;
    options["temperature"] = opts.temperature;
    options["top_p"] = opts.top_p;
    if (opts.max_tokens > 0) {
        options["num_predict"] = opts.max_tokens;
    }
    body["options"] = options;

    return body;
}

Result<LLMResponse> OllamaProvider::parse_response(const json& body) const {
    // Check for Ollama error response
    if (body.contains("error") && !body["error"].is_null()) {
        std::string err_msg;
        if (body["error"].is_string()) {
            err_msg = body["error"].get<std::string>();
        } else if (body["error"].is_object() && body["error"].contains("message")) {
            err_msg = body["error"]["message"].get<std::string>();
        } else {
            err_msg = body["error"].dump();
        }
        return Error::net("Ollama error: " + err_msg);
    }

    LLMResponse resp;

    // Content and tool calls from message object
    if (body.contains("message") && body["message"].is_object()) {
        const auto& msg = body["message"];

        // Content
        if (msg.contains("content") && !msg["content"].is_null()) {
            resp.content = msg["content"].get<std::string>();
        }

        // Tool calls
        if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
            for (const auto& tc : msg["tool_calls"]) {
                ToolCall call;
                // Ollama doesn't always provide an id; generate a sequential one
                call.id = tc.value("id", "");
                if (tc.contains("function") && tc["function"].is_object()) {
                    const auto& fn = tc["function"];
                    call.name = fn.value("name", "");
                    if (fn.contains("arguments")) {
                        const auto& args = fn["arguments"];
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
                }
                resp.tool_calls.push_back(std::move(call));
            }
        }
    }

    // Stop reason
    resp.stop_reason = body.value("done_reason", "");
    // If no done_reason but tool_calls exist, set to "tool_calls"
    if (resp.stop_reason.empty() && !resp.tool_calls.empty()) {
        resp.stop_reason = "tool_calls";
    }

    // Usage
    resp.usage.input_tokens = body.value("prompt_eval_count", 0);
    resp.usage.output_tokens = body.value("eval_count", 0);

    return resp;
}

Result<LLMResponse> OllamaProvider::chat(
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
    // No auth header — Ollama is local

    std::string url = strip_trailing_slash(config_.base_url) + "/api/chat";

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
        return Error::parse(std::string("Failed to parse Ollama response: ") + e.what());
    }
}

Result<void> OllamaProvider::stream_chat(
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
    // No auth header — Ollama is local

    std::string url = strip_trailing_slash(config_.base_url) + "/api/chat";

    // NDJSON buffer — Ollama sends newline-delimited JSON, not SSE
    std::string ndjson_buffer;
    int tool_call_counter = 0;

    auto on_data = [&](const std::string& data) {
        ndjson_buffer += data;

        // Process complete lines (each line is a JSON object)
        std::string::size_type pos = 0;
        while (true) {
            auto newline = ndjson_buffer.find('\n', pos);
            if (newline == std::string::npos) {
                break;
            }

            std::string line = ndjson_buffer.substr(pos, newline - pos);
            pos = newline + 1;

            // Skip empty lines
            if (line.empty() || (line.size() == 1 && line[0] == '\r')) {
                continue;
            }

            // Trim trailing \r if present (Windows line endings)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }

            try {
                auto chunk_json = json::parse(line);

                // Check for error in stream chunk
                if (chunk_json.contains("error") && !chunk_json["error"].is_null()) {
                    std::string err_msg;
                    if (chunk_json["error"].is_string()) {
                        err_msg = chunk_json["error"].get<std::string>();
                    } else {
                        err_msg = chunk_json["error"].dump();
                    }
                    StreamChunk err_chunk;
                    err_chunk.type = StreamChunk::Type::Error;
                    err_chunk.data = err_msg;
                    on_chunk(err_chunk);
                    continue;
                }

                // Content from message.content
                if (chunk_json.contains("message") && chunk_json["message"].is_object()) {
                    const auto& msg = chunk_json["message"];

                    if (msg.contains("content") && !msg["content"].is_null()) {
                        std::string text = msg["content"].get<std::string>();
                        if (!text.empty()) {
                            StreamChunk content_chunk;
                            content_chunk.type = StreamChunk::Type::Content;
                            content_chunk.data = text;
                            on_chunk(content_chunk);
                        }
                    }

                    // Tool calls in stream chunk
                    if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                        for (const auto& tc : msg["tool_calls"]) {
                            ToolCall call;
                            call.id = "tc_" + std::to_string(tool_call_counter++);
                            if (tc.contains("function") && tc["function"].is_object()) {
                                const auto& fn = tc["function"];
                                call.name = fn.value("name", "");
                                if (fn.contains("arguments")) {
                                    const auto& args = fn["arguments"];
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
                            }

                            StreamChunk tc_chunk;
                            tc_chunk.type = StreamChunk::Type::ToolCallEnd;
                            tc_chunk.tool_call = call;
                            on_chunk(tc_chunk);
                        }
                    }
                }

                // Done signal
                if (chunk_json.value("done", false)) {
                    StreamChunk done_chunk;
                    done_chunk.type = StreamChunk::Type::Done;

                    // Usage from final chunk
                    if (chunk_json.contains("prompt_eval_count") || chunk_json.contains("eval_count")) {
                        Usage u;
                        u.input_tokens = chunk_json.value("prompt_eval_count", 0);
                        u.output_tokens = chunk_json.value("eval_count", 0);
                        done_chunk.usage = u;
                    }

                    on_chunk(done_chunk);
                }

            } catch (const json::exception& e) {
                spdlog::warn("Failed to parse Ollama NDJSON line: {}", e.what());
            }
        }

        // Keep unprocessed remainder in buffer
        if (pos > 0) {
            ndjson_buffer = ndjson_buffer.substr(pos);
        }
    };

    auto result = client_.stream_post(url, on_data, req_opts);
    if (!result.ok()) {
        return result.error();
    }

    return {};
}

std::vector<std::string> OllamaProvider::list_models() const {
    std::string url = strip_trailing_slash(config_.base_url) + "/api/tags";

    net::RequestOptions req_opts;
    req_opts.timeout = config_.timeout;
    req_opts.tls = config_.tls;

    auto result = client_.get(url, req_opts);
    if (!result.ok()) {
        EA_WARN("Ollama list_models failed: {}", result.error().message);
        return {};
    }

    const auto& http_resp = result.value();
    if (http_resp.status != 200) {
        EA_WARN("Ollama list_models returned HTTP {}", http_resp.status);
        return {};
    }

    try {
        auto resp_json = json::parse(http_resp.body);
        std::vector<std::string> models;
        if (resp_json.contains("models") && resp_json["models"].is_array()) {
            for (const auto& m : resp_json["models"]) {
                if (m.contains("name") && m["name"].is_string()) {
                    models.push_back(m["name"].get<std::string>());
                }
            }
        }
        return models;
    } catch (const json::exception& e) {
        EA_WARN("Failed to parse Ollama /api/tags response: {}", e.what());
        return {};
    }
}

}  // namespace ea::provider
