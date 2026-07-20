// src/server/HttpServer.cpp
#include "HttpServer.h"
#include "common/io/Logger.h"
#include <httplib.h>
#include <chrono>
#include <set>
#include <thread>

namespace ea::server {

using json = nlohmann::json;

HttpServer::HttpServer(ServerConfig config,
                       IProvider* provider,
                       tool::ToolRegistry* registry,
                       security::SecurityPolicy* policy,
                       IMemory* shared_memory,
                       conversation::IConversationStore* conv_store)
    : config_(std::move(config))
    , server_(std::make_unique<httplib::Server>())
    , sessions_(config_, conv_store)
    , provider_(provider)
    , registry_(registry)
    , policy_(policy)
    , shared_memory_(shared_memory)
    , conv_store_(conv_store)
    , start_time_(std::chrono::steady_clock::now()) {
    setup_routes();
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::set_cors_headers(void* res_ptr) {
    auto& res = *static_cast<httplib::Response*>(res_ptr);
    res.set_header("Access-Control-Allow-Origin", config_.cors_origin);
    res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

json HttpServer::error_response(const std::string& code, const std::string& message) {
    return json{{"error", {{"code", code}, {"message", message}}}};
}

void HttpServer::setup_routes() {
    // CORS preflight for all /api/ routes
    server_->Options("/api/.*", [this](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(&res);
        res.status = 204;
    });

    // System
    server_->Get("/api/health", [this](const httplib::Request& req, httplib::Response& res) {
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
        auto sessions = sessions_.list();

        json body;
        body["status"] = "ok";
        body["uptime"] = uptime;
        body["sessions"] = sessions.size();

        set_cors_headers(&res);
        res.set_content(body.dump(), "application/json");
    });

    server_->Get("/api/models", [this](const httplib::Request&, httplib::Response& res) {
        auto models = provider_->list_models();
        json body = models;

        set_cors_headers(&res);
        res.set_content(body.dump(), "application/json");
    });

    // Sessions
    server_->Post("/api/sessions", [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            set_cors_headers(&res);
            res.status = 400;
            res.set_content(error_response("invalid_json", "Failed to parse request body").dump(), "application/json");
            return;
        }

        std::string model = body.value("model", "");
        std::string system_prompt = body.value("system_prompt", "");
        std::string conversation_id = body.value("conversation_id", "");

        AgentLoop::Config loop_cfg;
        auto* session = sessions_.create(provider_, registry_, shared_memory_, policy_, loop_cfg, model, system_prompt, conversation_id);

        if (!session) {
            set_cors_headers(&res);
            res.status = 429;
            res.set_content(error_response("max_sessions", "Maximum number of sessions reached").dump(), "application/json");
            return;
        }

        json resp;
        resp["id"] = session->id;
        resp["conversation_id"] = session->loop->conversation_id();
        resp["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        set_cors_headers(&res);
        res.set_content(resp.dump(), "application/json");
    });

    server_->Get("/api/sessions", [this](const httplib::Request&, httplib::Response& res) {
        auto sessions = sessions_.list();
        json arr = json::array();

        for (auto* s : sessions) {
            json obj;
            obj["id"] = s->id;
            obj["running"] = s->running.load();
            obj["last_active"] = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (!s->model.empty()) {
                obj["model"] = s->model;
            }
            arr.push_back(obj);
        }

        set_cors_headers(&res);
        res.set_content(arr.dump(), "application/json");
    });

    server_->Delete(R"(/api/sessions/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        // Check if session exists first to distinguish 404 from 409
        auto* session = sessions_.get(id);
        if (!session) {
            set_cors_headers(&res);
            res.status = 404;
            res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
            return;
        }

        // Session exists — check if it's running
        if (session->running.load()) {
            set_cors_headers(&res);
            res.status = 409;
            res.set_content(error_response("session_busy", "Session " + id + " is running and cannot be deleted").dump(), "application/json");
            return;
        }

        // Not running — safe to remove
        sessions_.remove(id);

        set_cors_headers(&res);
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    // Chat
    server_->Post(R"(/api/sessions/([^/]+)/chat)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        auto* session = sessions_.get(id);
        if (!session) {
            set_cors_headers(&res);
            res.status = 404;
            res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
            return;
        }

        // Check if session is already running
        bool expected = false;
        if (!session->running.compare_exchange_strong(expected, true)) {
            set_cors_headers(&res);
            res.status = 409;
            res.set_content(error_response("session_busy", "Session " + id + " is already processing a request").dump(), "application/json");
            return;
        }

        // Parse request body
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            session->running.store(false);
            set_cors_headers(&res);
            res.status = 400;
            res.set_content(error_response("invalid_json", "Failed to parse request body").dump(), "application/json");
            return;
        }

        if (!body.contains("message") || !body["message"].is_string()) {
            session->running.store(false);
            set_cors_headers(&res);
            res.status = 400;
            res.set_content(error_response("missing_field", "Request must contain a 'message' string field").dump(), "application/json");
            return;
        }

        std::string message = body["message"].get<std::string>();

        // Run the agent loop
        auto result = session->loop->run(message);
        session->running.store(false);

        if (!result.ok()) {
            set_cors_headers(&res);
            res.status = 500;
            res.set_content(error_response("agent_error", result.error().message).dump(), "application/json");
            return;
        }

        // Find the last assistant message in history
        std::string content;
        const auto& history = session->loop->history();
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (it->role == Role::Assistant) {
                content = it->content;
                break;
            }
        }

        json resp;
        resp["content"] = content;
        resp["stop_reason"] = "stop";

        set_cors_headers(&res);
        res.set_content(resp.dump(), "application/json");
    });

    server_->Get(R"(/api/sessions/([^/]+)/stream)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        auto* session = sessions_.get(id);
        if (!session) {
            set_cors_headers(&res);
            res.status = 404;
            res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
            return;
        }

        std::string message = req.get_param_value("message");
        if (message.empty()) {
            set_cors_headers(&res);
            res.status = 400;
            res.set_content(error_response("missing_field", "Query parameter 'message' is required").dump(), "application/json");
            return;
        }

        // Check if session is already running
        bool expected = false;
        if (!session->running.compare_exchange_strong(expected, true)) {
            set_cors_headers(&res);
            res.status = 409;
            res.set_content(error_response("session_busy", "Session " + id + " is already processing a request").dump(), "application/json");
            return;
        }

        set_cors_headers(&res);

        // SSE: run the agent loop and wrap the response in SSE format.
        // NOTE: AgentLoop doesn't currently support setting StreamFn after construction,
        // so this works like the chat endpoint but wraps the response in SSE events.
        // A future enhancement can add proper streaming by reconstructing the AgentLoop with a StreamFn.
        res.set_chunked_content_provider(
            "text/event-stream",
            [this, session, message](size_t /*offset*/, httplib::DataSink& sink) -> bool {
                auto result = session->loop->run(message);

                if (!result.ok()) {
                    // Send error event
                    json err;
                    err["type"] = "error";
                    err["error"] = result.error().message;
                    std::string err_data = "data: " + err.dump() + "\n\n";
                    sink.write(err_data.data(), err_data.size());
                } else {
                    // Find the last assistant message
                    std::string content;
                    const auto& history = session->loop->history();
                    for (auto it = history.rbegin(); it != history.rend(); ++it) {
                        if (it->role == Role::Assistant) {
                            content = it->content;
                            break;
                        }
                    }

                    // Send done event with the full content
                    json done;
                    done["type"] = "done";
                    done["content"] = content;
                    done["stop_reason"] = "stop";
                    std::string done_data = "data: " + done.dump() + "\n\n";
                    sink.write(done_data.data(), done_data.size());
                }

                sink.done();
                return true;
            },
            [session](bool /*success*/) {
                session->running.store(false);
            }
        );
    });

    server_->Post(R"(/api/sessions/([^/]+)/interrupt)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        auto* session = sessions_.get(id);
        if (!session) {
            set_cors_headers(&res);
            res.status = 404;
            res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
            return;
        }

        session->loop->interrupt();

        set_cors_headers(&res);
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    server_->Get(R"(/api/sessions/([^/]+)/history)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        auto* session = sessions_.get(id);
        if (!session) {
            set_cors_headers(&res);
            res.status = 404;
            res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
            return;
        }

        const auto& history = session->loop->history();
        json messages = json::array();

        for (const auto& msg : history) {
            json m;
            // Map Role enum to string
            switch (msg.role) {
                case Role::System:    m["role"] = "system"; break;
                case Role::User:      m["role"] = "user"; break;
                case Role::Assistant: m["role"] = "assistant"; break;
                case Role::Tool:      m["role"] = "tool"; break;
            }
            m["content"] = msg.content;

            if (msg.name.has_value()) {
                m["name"] = msg.name.value();
            }
            if (msg.tool_calls.has_value()) {
                json tc_arr = json::array();
                for (const auto& tc : msg.tool_calls.value()) {
                    json tc_obj;
                    tc_obj["id"] = tc.id;
                    tc_obj["name"] = tc.name;
                    tc_obj["arguments"] = tc.arguments;
                    tc_arr.push_back(tc_obj);
                }
                m["tool_calls"] = tc_arr;
            }
            if (msg.tool_call_id.has_value()) {
                m["tool_call_id"] = msg.tool_call_id.value();
            }

            messages.push_back(m);
        }

        json body;
        body["messages"] = messages;

        set_cors_headers(&res);
        res.set_content(body.dump(), "application/json");
    });

    // Approvals
    server_->Get("/api/approvals", [this](const httplib::Request&, httplib::Response& res) {
        auto sessions = sessions_.list();
        json arr = json::array();

        for (auto* session : sessions) {
            if (!session->approval) continue;
            auto pending = session->approval->pending_list();
            for (auto* pa : pending) {
                json obj;
                obj["id"] = pa->id;
                obj["session_id"] = session->id;
                obj["tool"] = pa->request.tool_name;
                obj["arguments"] = pa->request.arguments;
                obj["description"] = pa->request.description;
                arr.push_back(obj);
            }
        }

        set_cors_headers(&res);
        res.set_content(arr.dump(), "application/json");
    });

    server_->Post(R"(/api/approvals/([^/]+)/resolve)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string approval_id = req.matches[1];

        // Parse request body
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            set_cors_headers(&res);
            res.status = 400;
            res.set_content(error_response("invalid_json", "Failed to parse request body").dump(), "application/json");
            return;
        }

        if (!body.contains("decision") || !body["decision"].is_string()) {
            set_cors_headers(&res);
            res.status = 400;
            res.set_content(error_response("missing_field", "Request must contain a 'decision' string field").dump(), "application/json");
            return;
        }

        std::string decision_str = body["decision"].get<std::string>();
        security::ApprovalDecision decision;
        if (decision_str == "approved") {
            decision = security::ApprovalDecision::Approved;
        } else if (decision_str == "rejected") {
            decision = security::ApprovalDecision::Rejected;
        } else if (decision_str == "aborted") {
            decision = security::ApprovalDecision::Aborted;
        } else {
            set_cors_headers(&res);
            res.status = 400;
            res.set_content(error_response("invalid_decision", "Decision must be 'approved', 'rejected', or 'aborted'").dump(), "application/json");
            return;
        }

        // If session_id query parameter is provided, only search that specific session.
        // This avoids resolving an approval in the wrong session when IDs collide
        // (each session's PendingApprovalHandler starts next_id_ at 1).
        // If session_id is not provided, search all sessions (may resolve wrong session
        // if approval IDs collide across sessions).
        std::string session_id = req.get_param_value("session_id");
        bool found = false;

        if (!session_id.empty()) {
            auto* session = sessions_.get(session_id);
            if (session && session->approval) {
                found = session->approval->resolve(approval_id, decision);
            }
        } else {
            auto sessions = sessions_.list();
            for (auto* session : sessions) {
                if (!session->approval) continue;
                if (session->approval->resolve(approval_id, decision)) {
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            set_cors_headers(&res);
            res.status = 404;
            res.set_content(error_response("approval_not_found", "Approval " + approval_id + " not found").dump(), "application/json");
            return;
        }

        set_cors_headers(&res);
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    server_->Get("/api/approvals/stream", [this](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(&res);

        res.set_chunked_content_provider(
            "text/event-stream",
            [this](size_t /*offset*/, httplib::DataSink& sink) -> bool {
                // Send initial connected comment
                std::string connected = ": connected\n\n";
                sink.write(connected.data(), connected.size());

                std::set<std::string> sent_ids;

                // Poll for up to 5 minutes (150 iterations * 2 seconds)
                for (int i = 0; i < 150; ++i) {
                    auto sessions = sessions_.list();
                    for (auto* session : sessions) {
                        if (!session->approval) continue;
                        auto pending = session->approval->pending_list();
                        for (auto* pa : pending) {
                            if (sent_ids.count(pa->id)) continue;
                            sent_ids.insert(pa->id);

                            json evt;
                            evt["type"] = "approval_request";
                            evt["id"] = pa->id;
                            evt["session_id"] = session->id;
                            evt["tool"] = pa->request.tool_name;
                            evt["arguments"] = pa->request.arguments;
                            evt["description"] = pa->request.description;

                            std::string data = "data: " + evt.dump() + "\n\n";
                            sink.write(data.data(), data.size());
                        }
                    }

                    std::this_thread::sleep_for(std::chrono::seconds(2));
                }

                sink.done();
                return true;
            }
        );
    });

    // --- Conversation API ---
    if (conv_store_) {
        // List conversations
        server_->Get("/api/conversations", [this](const httplib::Request& req, httplib::Response& res) {
            int limit = 50;
            int offset = 0;
            try {
                if (!req.get_param_value("limit").empty()) limit = std::stoi(req.get_param_value("limit"));
                if (!req.get_param_value("offset").empty()) offset = std::stoi(req.get_param_value("offset"));
            } catch (...) {
                // Use defaults on parse error
            }
            if (limit < 1) limit = 1;
            if (limit > 100) limit = 100;
            if (offset < 0) offset = 0;

            auto list = conv_store_->list(limit, offset);
            json arr = json::array();
            if (list.ok()) {
                for (const auto& m : list.value()) {
                    json obj;
                    obj["id"] = m.id;
                    obj["title"] = m.title;
                    obj["model"] = m.model;
                    obj["created_at"] = m.created_at;
                    obj["updated_at"] = m.updated_at;
                    obj["message_count"] = m.message_count;
                    arr.push_back(obj);
                }
            }
            set_cors_headers(&res);
            res.set_content(json{{"conversations", arr}}.dump(), "application/json");
        });

        // Get conversation messages (must be registered before the less-specific GET below)
        server_->Get(R"(/api/conversations/([^/]+)/messages)", [this](const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];
            auto msgs = conv_store_->load(id);
            if (!msgs.ok()) {
                set_cors_headers(&res);
                res.status = 404;
                res.set_content(error_response("not_found", "Conversation " + id + " not found").dump(), "application/json");
                return;
            }
            json messages = json::array();
            for (const auto& msg : msgs.value()) {
                json m;
                switch (msg.role) {
                    case Role::System:    m["role"] = "system"; break;
                    case Role::User:      m["role"] = "user"; break;
                    case Role::Assistant: m["role"] = "assistant"; break;
                    case Role::Tool:      m["role"] = "tool"; break;
                }
                m["content"] = msg.content;
                if (msg.name.has_value()) m["name"] = msg.name.value();
                if (msg.tool_calls.has_value()) {
                    json tc_arr = json::array();
                    for (const auto& tc : msg.tool_calls.value()) {
                        json tc_obj;
                        tc_obj["id"] = tc.id;
                        tc_obj["name"] = tc.name;
                        tc_obj["arguments"] = tc.arguments;
                        tc_arr.push_back(tc_obj);
                    }
                    m["tool_calls"] = tc_arr;
                }
                if (msg.tool_call_id.has_value()) m["tool_call_id"] = msg.tool_call_id.value();
                messages.push_back(m);
            }
            set_cors_headers(&res);
            res.set_content(json{{"messages", messages}}.dump(), "application/json");
        });

        // Export conversation as JSONL (must be registered before the less-specific GET below)
        server_->Get(R"(/api/conversations/([^/]+)/export)", [this](const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];
            auto data = conv_store_->export_jsonl(id);
            if (!data.ok()) {
                set_cors_headers(&res);
                res.status = 404;
                res.set_content(error_response("not_found", "Conversation " + id + " not found").dump(), "application/json");
                return;
            }
            set_cors_headers(&res);
            res.set_content(data.value(), "application/x-jsonl");
        });

        // Get conversation metadata (less specific — registered after /messages and /export)
        server_->Get(R"(/api/conversations/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];
            auto meta = conv_store_->get_meta(id);
            if (!meta.ok()) {
                set_cors_headers(&res);
                res.status = 404;
                res.set_content(error_response("not_found", "Conversation " + id + " not found").dump(), "application/json");
                return;
            }
            json obj;
            obj["id"] = meta.value().id;
            obj["title"] = meta.value().title;
            obj["model"] = meta.value().model;
            obj["created_at"] = meta.value().created_at;
            obj["updated_at"] = meta.value().updated_at;
            obj["message_count"] = meta.value().message_count;
            set_cors_headers(&res);
            res.set_content(obj.dump(), "application/json");
        });

        // Delete conversation
        server_->Delete(R"(/api/conversations/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];
            auto r = conv_store_->remove(id);
            if (!r.ok()) {
                set_cors_headers(&res);
                res.status = 500;
                res.set_content(error_response("delete_failed", r.error().message).dump(), "application/json");
                return;
            }
            set_cors_headers(&res);
            res.set_content(json{{"ok", true}, {"deleted", r.value()}}.dump(), "application/json");
        });

        // Import conversation from JSONL
        server_->Post("/api/conversations/import", [this](const httplib::Request& req, httplib::Response& res) {
            json body;
            try {
                body = json::parse(req.body);
            } catch (...) {
                set_cors_headers(&res);
                res.status = 400;
                res.set_content(error_response("invalid_json", "Failed to parse request body").dump(), "application/json");
                return;
            }
            if (!body.contains("data") || !body["data"].is_string()) {
                set_cors_headers(&res);
                res.status = 400;
                res.set_content(error_response("missing_field", "Request must contain a 'data' string field").dump(), "application/json");
                return;
            }
            std::string jsonl_data = body["data"].get<std::string>();
            std::string model = body.value("model", "");
            auto new_id = conv_store_->import_jsonl(jsonl_data, model);
            if (!new_id.ok()) {
                set_cors_headers(&res);
                res.status = 400;
                res.set_content(error_response("import_failed", new_id.error().message).dump(), "application/json");
                return;
            }
            set_cors_headers(&res);
            res.set_content(json{{"id", new_id.value()}}.dump(), "application/json");
        });
    }
}

// --- Server lifecycle ---

void HttpServer::start() {
    EA_INFO("Server starting on {}:{}", config_.host, config_.port);

    if (config_.port == 0) {
        // Let OS pick a port
        bound_port_ = server_->bind_to_any_port(config_.host);
        if (bound_port_ <= 0) {
            EA_ERROR("Server failed to bind to any port on {}", config_.host);
            return;
        }
        if (!server_->listen_after_bind()) {
            EA_ERROR("Server failed to listen after bind on {}:{}", config_.host, bound_port_);
        }
    } else {
        if (!server_->listen(config_.host, config_.port)) {
            EA_ERROR("Server failed to listen on {}:{}", config_.host, config_.port);
        }
        bound_port_ = config_.port;
    }
}

void HttpServer::stop() {
    server_->stop();
}

int HttpServer::bound_port() const {
    return bound_port_;
}

}  // namespace ea::server
