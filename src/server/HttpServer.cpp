// src/server/HttpServer.cpp
#include "HttpServer.h"
#include "common/io/Logger.h"
#include <httplib.h>
#include <chrono>

namespace ea::server {

using json = nlohmann::json;

HttpServer::HttpServer(ServerConfig config,
                       IProvider* provider,
                       tool::ToolRegistry* registry,
                       security::SecurityPolicy* policy,
                       IMemory* shared_memory)
    : config_(std::move(config))
    , server_(std::make_unique<httplib::Server>())
    , sessions_(config_)
    , provider_(provider)
    , registry_(registry)
    , policy_(policy)
    , shared_memory_(shared_memory)
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

        AgentLoop::Config loop_cfg;
        auto* session = sessions_.create(provider_, registry_, shared_memory_, policy_, loop_cfg, model, system_prompt);

        if (!session) {
            set_cors_headers(&res);
            res.status = 429;
            res.set_content(error_response("max_sessions", "Maximum number of sessions reached").dump(), "application/json");
            return;
        }

        json resp;
        resp["id"] = session->id;
        resp["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(
            session->last_active.time_since_epoch()).count();

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
                s->last_active.time_since_epoch()).count();
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
        if (!sessions_.remove(id)) {
            set_cors_headers(&res);
            res.status = 404;
            res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
            return;
        }

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
        // Full implementation in Task 4
        set_cors_headers(&res);
        res.set_content(error_response("not_implemented", "Chat endpoint not yet implemented").dump(), "application/json");
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
        // Full implementation in Task 4
        set_cors_headers(&res);
        res.set_content(error_response("not_implemented", "Stream endpoint not yet implemented").dump(), "application/json");
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
        // Full implementation in Task 4
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
        // Full implementation in Task 4
        json body;
        body["messages"] = json::array();
        set_cors_headers(&res);
        res.set_content(body.dump(), "application/json");
    });

    // Approvals
    server_->Get("/api/approvals", [this](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(&res);
        res.set_content(json::array().dump(), "application/json");
    });

    server_->Post(R"(/api/approvals/([^/]+)/resolve)", [this](const httplib::Request&, httplib::Response& res) {
        // Full implementation in Task 5
        set_cors_headers(&res);
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    server_->Get("/api/approvals/stream", [this](const httplib::Request&, httplib::Response& res) {
        // Full implementation in Task 5
        set_cors_headers(&res);
        res.set_content(error_response("not_implemented", "Approval stream not yet implemented").dump(), "application/json");
    });
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
