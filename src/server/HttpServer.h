// src/server/HttpServer.h
// HTTP API server wrapping httplib::Server with session CRUD, health, models,
// CORS, and stubs for chat/approval endpoints (Tasks 4-5 fill those in).
#pragma once
#include "ServerConfig.h"
#include "SessionManager.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "security/SecurityPolicy.h"
#include "conversation/IConversationStore.h"
#include "budget/BudgetTracker.h"
#include "nlohmann/json.hpp"
#include <string>
#include <chrono>
#include <memory>
#include <functional>

namespace httplib { class Server; }

namespace ea::server {

class HttpServer {
public:
    HttpServer(ServerConfig config,
               IProvider* provider,
               tool::ToolRegistry* registry,
               security::SecurityPolicy* policy,
               IMemory* shared_memory = nullptr,
               conversation::IConversationStore* conv_store = nullptr,
               budget::BudgetTracker* budget_tracker = nullptr);
    ~HttpServer();

    void start();   // blocking
    void stop();    // graceful shutdown
    int bound_port() const;  // actual port (useful when port=0)

    // Test access
    SessionManager& test_sessions() { return sessions_; }

private:
    void setup_routes();
    void set_cors_headers(void* res);  // httplib::Response* — void* to avoid header dep
    nlohmann::json error_response(const std::string& code, const std::string& message);

    ServerConfig config_;
    std::unique_ptr<httplib::Server> server_;
    SessionManager sessions_;
    IProvider* provider_;
    tool::ToolRegistry* registry_;
    security::SecurityPolicy* policy_;
    IMemory* shared_memory_;
    conversation::IConversationStore* conv_store_;
    budget::BudgetTracker* budget_tracker_;
    std::chrono::steady_clock::time_point start_time_;
    int bound_port_ = 0;
};

}  // namespace ea::server
