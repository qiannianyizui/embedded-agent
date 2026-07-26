// tests/test_http_server.cpp
#include <catch2/catch_test_macros.hpp>
#include "server/HttpServer.h"
#include "server/ServerConfig.h"
#include "memory/InMemoryBackend.h"
#include "tool/ToolRegistry.h"
#include "security/SecurityPolicy.h"
#include "provider/IProvider.h"
#include "core/Types.h"
#include "nlohmann/json.hpp"
#include <httplib.h>
#include <thread>
#include <chrono>

using namespace ea;
using namespace ea::server;
using namespace ea::memory;
using json = nlohmann::json;

// Minimal mock provider for testing (unique name to avoid ODR violations
// with MockProvider in other test files)
class HttpTestProvider : public IProvider {
public:
    std::string name() const override { return "http-test"; }
    std::vector<std::string> list_models() const override { return {"http-test-model"}; }
    provider::ProviderCapabilities capabilities() const override { return {true, true, false, false, false}; }
    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        return LLMResponse{"mock response", {}, {}, "stop"};
    }
    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override { return {}; }
};

// Helper: start server on a random available port
struct ServerFixture {
    std::shared_ptr<HttpTestProvider> provider;
    std::shared_ptr<tool::ToolRegistry> registry;
    std::shared_ptr<security::SecurityPolicy> policy;
    std::shared_ptr<InMemoryBackend> backend;
    std::unique_ptr<HttpServer> server;
    std::thread server_thread;
    int port;

    ServerFixture() {
        provider = std::make_shared<HttpTestProvider>();
        registry = std::make_shared<tool::ToolRegistry>();
        policy = std::make_shared<security::SecurityPolicy>();
        backend = std::make_shared<InMemoryBackend>();

        ServerConfig cfg;
        cfg.host = "127.0.0.1";
        cfg.port = 0;  // Let OS pick a port
        server = std::make_unique<HttpServer>(cfg, provider.get(), registry.get(), policy.get(), backend.get());

        // Start server in background
        server_thread = std::thread([this]() { server->start(); });

        // Wait for server to be ready by polling the health endpoint
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        port = server->bound_port();

        // Retry connecting until the server is actually accepting
        for (int i = 0; i < 20; ++i) {
            httplib::Client probe("http://127.0.0.1:" + std::to_string(port));
            auto probe_res = probe.Get("/api/health");
            if (probe_res && probe_res->status == 200) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    ~ServerFixture() {
        server->stop();
        if (server_thread.joinable()) server_thread.join();
    }

    std::string base_url() const {
        return "http://127.0.0.1:" + std::to_string(port);
    }
};

TEST_CASE("Health endpoint returns ok", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Get("/api/health");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body["status"] == "ok");
    REQUIRE(body.contains("uptime"));
    REQUIRE(body.contains("sessions"));
}

TEST_CASE("Create session returns session ID", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json req;
    req["model"] = "http-test-model";

    auto res = cli.Post("/api/sessions", req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.contains("id"));
    REQUIRE(body["id"].get<std::string>().substr(0, 5) == "sess_");
}

TEST_CASE("List sessions returns array", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    // Create a session first
    json req = json::object();
    auto create_res = cli.Post("/api/sessions", req.dump(), "application/json");
    REQUIRE(create_res != nullptr);
    REQUIRE(create_res->status == 200);

    auto res = cli.Get("/api/sessions");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.is_array());
    REQUIRE(body.size() >= 1);
    REQUIRE(body[0].contains("id"));
    REQUIRE(body[0].contains("running"));
}

TEST_CASE("Delete session removes it", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json req = json::object();
    auto create_res = cli.Post("/api/sessions", req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string id = create_body["id"];

    auto del_res = cli.Delete("/api/sessions/" + id);
    REQUIRE(del_res != nullptr);
    REQUIRE(del_res->status == 200);

    auto del_body = json::parse(del_res->body);
    REQUIRE(del_body["ok"] == true);

    // Verify it's gone
    auto list_res = cli.Get("/api/sessions");
    auto list_body = json::parse(list_res->body);
    bool found = false;
    for (const auto& s : list_body) {
        if (s["id"] == id) found = true;
    }
    REQUIRE_FALSE(found);
}

TEST_CASE("Delete nonexistent session returns 404", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Delete("/api/sessions/nonexistent");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 404);
}

TEST_CASE("Chat on nonexistent session returns 404", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json req;
    req["message"] = "Hello";

    auto res = cli.Post("/api/sessions/nonexistent/chat", req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 404);
}

TEST_CASE("Models endpoint returns list", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Get("/api/models");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.is_array());
    REQUIRE(body.size() >= 1);
}

TEST_CASE("CORS headers present", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Get("/api/health");
    REQUIRE(res != nullptr);
    REQUIRE(res->has_header("Access-Control-Allow-Origin"));
}

TEST_CASE("Chat endpoint returns response", "[server][http][chat]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json create_req = json::object();
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string id = create_body["id"];

    json chat_req;
    chat_req["message"] = "Hello";

    auto res = cli.Post("/api/sessions/" + id + "/chat", chat_req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.contains("content"));
}

TEST_CASE("Chat with running session returns 409", "[server][http][chat]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json create_req = json::object();
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string id = create_body["id"];

    // Manually set running flag to simulate active session
    auto* session = fx.server->test_sessions().get(id);
    session->running.store(true);

    json chat_req;
    chat_req["message"] = "Hello";

    auto res = cli.Post("/api/sessions/" + id + "/chat", chat_req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 409);
}

TEST_CASE("Interrupt endpoint returns ok", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json create_req = json::object();
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string id = create_body["id"];

    auto res = cli.Post("/api/sessions/" + id + "/interrupt");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body["ok"] == true);
}

TEST_CASE("History endpoint returns messages array", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json create_req = json::object();
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string id = create_body["id"];

    auto res = cli.Get("/api/sessions/" + id + "/history");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.contains("messages"));
    REQUIRE(body["messages"].is_array());
}

TEST_CASE("Approvals list returns array", "[server][http][approval]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Get("/api/approvals");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.is_array());
}

TEST_CASE("Resolve nonexistent approval returns 404", "[server][http][approval]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    // Create a session so we have a valid session_id to scope the query
    json create_req = json::object();
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string session_id = create_body["id"];

    json req;
    req["decision"] = "approved";

    auto res = cli.Post("/api/approvals/999/resolve?session_id=" + session_id, req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 404);
}

TEST_CASE("Resolve approval with invalid decision returns 400", "[server][http][approval]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json req;
    req["decision"] = "maybe";

    auto res = cli.Post("/api/approvals/1/resolve", req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 400);

    auto body = json::parse(res->body);
    REQUIRE(body.contains("error"));
}

TEST_CASE("Resolve approval with missing decision returns 400", "[server][http][approval]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json req = json::object();

    auto res = cli.Post("/api/approvals/1/resolve", req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 400);
}

TEST_CASE("Resolve approval with invalid JSON returns 400", "[server][http][approval]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Post("/api/approvals/1/resolve", "not json", "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 400);
}

TEST_CASE("Approvals list includes pending approvals from sessions", "[server][http][approval]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    // Create a session
    json create_req = json::object();
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string session_id = create_body["id"];

    // Get the session and inject a pending approval directly
    auto* session = fx.server->test_sessions().get(session_id);
    REQUIRE(session != nullptr);
    REQUIRE(session->approval != nullptr);

    // Request an approval in a separate thread (it blocks until resolved)
    std::thread approval_thread([session]() {
        security::ApprovalRequest ar;
        ar.tool_name = "shell";
        ar.arguments = json{{"command", "ls -la"}};
        ar.description = "List files";
        session->approval->request_approval(ar);
    });

    // Give the approval thread time to register the pending approval
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // List approvals — should see the pending one
    auto res = cli.Get("/api/approvals");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.is_array());
    REQUIRE(body.size() >= 1);

    bool found = false;
    for (const auto& a : body) {
        if (a["tool"] == "shell" && a["session_id"] == session_id) {
            found = true;
            REQUIRE(a.contains("id"));
            REQUIRE(a.contains("arguments"));
            REQUIRE(a.contains("description"));
            REQUIRE(a["description"] == "List files");
            break;
        }
    }
    REQUIRE(found);

    // Resolve the approval so the thread can finish
    std::string approval_id;
    for (const auto& a : body) {
        if (a["tool"] == "shell" && a["session_id"] == session_id) {
            approval_id = a["id"].get<std::string>();
            break;
        }
    }

    json resolve_req;
    resolve_req["decision"] = "approved";
    auto resolve_res = cli.Post("/api/approvals/" + approval_id + "/resolve?session_id=" + session_id,
                                resolve_req.dump(), "application/json");
    REQUIRE(resolve_res != nullptr);
    REQUIRE(resolve_res->status == 200);

    auto resolve_body = json::parse(resolve_res->body);
    REQUIRE(resolve_body["ok"] == true);

    approval_thread.join();
}

TEST_CASE("Resolve approval with rejected decision", "[server][http][approval]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    // Create a session
    json create_req = json::object();
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string session_id = create_body["id"];

    auto* session = fx.server->test_sessions().get(session_id);

    // Request an approval in a separate thread
    std::thread approval_thread([session]() {
        security::ApprovalRequest ar;
        ar.tool_name = "file_write";
        ar.arguments = json{{"path", "/tmp/test"}};
        ar.description = "Write file";
        session->approval->request_approval(ar);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Get the approval ID
    auto list_res = cli.Get("/api/approvals");
    auto list_body = json::parse(list_res->body);
    std::string approval_id;
    for (const auto& a : list_body) {
        if (a["tool"] == "file_write") {
            approval_id = a["id"].get<std::string>();
            break;
        }
    }
    REQUIRE_FALSE(approval_id.empty());

    // Resolve with "rejected"
    json resolve_req;
    resolve_req["decision"] = "rejected";
    auto resolve_res = cli.Post("/api/approvals/" + approval_id + "/resolve?session_id=" + session_id,
                                resolve_req.dump(), "application/json");
    REQUIRE(resolve_res != nullptr);
    REQUIRE(resolve_res->status == 200);

    approval_thread.join();
}
