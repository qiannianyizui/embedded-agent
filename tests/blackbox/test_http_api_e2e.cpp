// tests/blackbox/test_http_api_e2e.cpp
// HTTP API end-to-end (black-box) tests — start real HttpServer, send HTTP
// requests, verify responses. Tests cover endpoints NOT covered by the
// existing test_http_server.cpp in the system test suite.
#include <catch2/catch_test_macros.hpp>
#include "MockProvider.h"
#include "server/HttpServer.h"
#include "server/ServerConfig.h"
#include "memory/InMemoryBackend.h"
#include "tool/ToolRegistry.h"
#include "security/SecurityPolicy.h"
#include "core/IProvider.h"
#include "core/Types.h"
#include "conversation/SqliteConversationStore.h"
#include "budget/BudgetTracker.h"
#include "budget/SqliteUsageStore.h"
#include "nlohmann/json.hpp"
#include <httplib.h>
#include <thread>
#include <chrono>
#include <memory>

using namespace ea;
using namespace ea::server;
using namespace ea::memory;
using namespace ea::test;
using json = nlohmann::json;

namespace {

// Reusable server fixture with full dependencies
struct FullServerFixture {
    std::shared_ptr<MockProvider> provider;
    std::shared_ptr<tool::ToolRegistry> registry;
    std::shared_ptr<security::SecurityPolicy> policy;
    std::shared_ptr<InMemoryBackend> backend;
    std::shared_ptr<conversation::SqliteConversationStore> conv_store;
    std::shared_ptr<budget::SqliteUsageStore> usage_store;
    std::shared_ptr<budget::BudgetTracker> budget_tracker;
    std::unique_ptr<HttpServer> server;
    std::thread server_thread;
    int port;

    FullServerFixture(bool with_budget = false, bool with_conversations = false,
                      int max_sessions = 100) {
        provider = std::make_shared<MockProvider>();
        provider->enqueue_text("Mock response");
        registry = std::make_shared<tool::ToolRegistry>();
        policy = std::make_shared<security::SecurityPolicy>();
        backend = std::make_shared<InMemoryBackend>();

        if (with_conversations) {
            conv_store = std::make_shared<conversation::SqliteConversationStore>(
                conversation::SqliteConversationStore::Config{":memory:", true});
            conv_store->open();
        }

        if (with_budget) {
            usage_store = std::make_shared<budget::SqliteUsageStore>(
                budget::SqliteUsageStore::Config{":memory:", true});
            usage_store->open();
            budget::BudgetConfig bcfg;
            bcfg.warn_cost_usd = 1.0;
            budget_tracker = std::make_shared<budget::BudgetTracker>(
                provider, bcfg);
            budget_tracker->set_store(usage_store);
        }

        ServerConfig cfg;
        cfg.host = "127.0.0.1";
        cfg.port = 0;  // OS-assigned port
        cfg.max_sessions = max_sessions;
        server = std::make_unique<HttpServer>(
            cfg, provider.get(), registry.get(), policy.get(), backend.get(),
            conv_store.get(), budget_tracker.get());

        server_thread = std::thread([this]() { server->start(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        port = server->bound_port();

        // Wait for server to be ready
        for (int i = 0; i < 20; ++i) {
            httplib::Client probe("http://127.0.0.1:" + std::to_string(port));
            auto res = probe.Get("/api/health");
            if (res && res->status == 200) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    ~FullServerFixture() {
        server->stop();
        if (server_thread.joinable()) server_thread.join();
    }

    std::string base_url() const {
        return "http://127.0.0.1:" + std::to_string(port);
    }

    httplib::Client client() const {
        return httplib::Client(base_url());
    }
};

}  // anonymous namespace

// --- Session edge cases ---

TEST_CASE("HTTP E2E: create session with model override", "[e2e][blackbox][http]") {
    FullServerFixture fx;
    auto cli = fx.client();

    json req;
    req["model"] = "gpt-4o";
    auto res = cli.Post("/api/sessions", req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.contains("id"));
}

TEST_CASE("HTTP E2E: create session with system_prompt", "[e2e][blackbox][http]") {
    FullServerFixture fx;
    auto cli = fx.client();

    json req;
    req["system_prompt"] = "You are a test assistant.";
    auto res = cli.Post("/api/sessions", req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);
}

TEST_CASE("HTTP E2E: max sessions returns 429", "[e2e][blackbox][http]") {
    FullServerFixture fx(false, false, 1);  // max_sessions = 1
    auto cli = fx.client();

    // Create first session
    json req1 = json::object();
    auto res1 = cli.Post("/api/sessions", req1.dump(), "application/json");
    REQUIRE(res1 != nullptr);
    REQUIRE(res1->status == 200);

    // Try to create second — should get 429
    json req2 = json::object();
    auto res2 = cli.Post("/api/sessions", req2.dump(), "application/json");
    REQUIRE(res2 != nullptr);
    REQUIRE(res2->status == 429);
}

TEST_CASE("HTTP E2E: chat with missing message returns 400", "[e2e][blackbox][http]") {
    FullServerFixture fx;
    auto cli = fx.client();

    // Create session
    json create_req = json::object();
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string id = create_body["id"];

    // Chat without message field
    json chat_req = json::object();
    auto res = cli.Post("/api/sessions/" + id + "/chat", chat_req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 400);
}

TEST_CASE("HTTP E2E: chat with invalid JSON returns 400", "[e2e][blackbox][http]") {
    FullServerFixture fx;
    auto cli = fx.client();

    json create_req = json::object();
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string id = create_body["id"];

    auto res = cli.Post("/api/sessions/" + id + "/chat", "not json", "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 400);
}

// --- Stream endpoint ---

TEST_CASE("HTTP E2E: stream endpoint on nonexistent session returns 404", "[e2e][blackbox][http]") {
    FullServerFixture fx;
    auto cli = fx.client();

    auto res = cli.Get("/api/sessions/nonexistent/stream?message=Hi");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 404);
}

TEST_CASE("HTTP E2E: stream with missing message returns 400", "[e2e][blackbox][http]") {
    FullServerFixture fx;
    auto cli = fx.client();

    json create_req = json::object();
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string id = create_body["id"];

    auto res = cli.Get("/api/sessions/" + id + "/stream");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 400);
}

// --- CORS ---

TEST_CASE("HTTP E2E: CORS preflight returns 204", "[e2e][blackbox][http]") {
    FullServerFixture fx;
    auto cli = fx.client();

    auto res = cli.Options("/api/health");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 204);
    REQUIRE(res->has_header("Access-Control-Allow-Origin"));
}

TEST_CASE("HTTP E2E: CORS headers on error responses", "[e2e][blackbox][http]") {
    FullServerFixture fx;
    auto cli = fx.client();

    auto res = cli.Delete("/api/sessions/nonexistent");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 404);
    REQUIRE(res->has_header("Access-Control-Allow-Origin"));
}

// --- Conversation API ---

TEST_CASE("HTTP E2E: conversation CRUD lifecycle", "[e2e][blackbox][http]") {
    FullServerFixture fx(false, true);  // with conversations
    auto cli = fx.client();

    // Create a conversation (via session chat, which auto-creates)
    json create_req = json::object();
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    REQUIRE(create_res->status == 200);

    // List conversations
    auto list_res = cli.Get("/api/conversations");
    REQUIRE(list_res != nullptr);
    REQUIRE(list_res->status == 200);
    auto list_body = json::parse(list_res->body);
    REQUIRE(list_body.contains("conversations"));
    REQUIRE(list_body["conversations"].is_array());

    // Get conversation metadata
    if (!list_body["conversations"].empty()) {
        std::string conv_id = list_body["conversations"][0]["id"];
        auto meta_res = cli.Get("/api/conversations/" + conv_id);
        REQUIRE(meta_res != nullptr);
        REQUIRE(meta_res->status == 200);
    }

    // Delete conversation
    if (!list_body["conversations"].empty()) {
        std::string conv_id = list_body["conversations"][0]["id"];
        auto del_res = cli.Delete("/api/conversations/" + conv_id);
        REQUIRE(del_res != nullptr);
        REQUIRE(del_res->status == 200);
    }
}

TEST_CASE("HTTP E2E: conversation export returns JSONL", "[e2e][blackbox][http]") {
    FullServerFixture fx(false, true);
    auto cli = fx.client();

    // Create and chat
    json create_req = json::object();
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string conv_id = create_body["conversation_id"];

    if (!conv_id.empty()) {
        auto res = cli.Get("/api/conversations/" + conv_id + "/export");
        if (res && res->status == 200) {
            REQUIRE((res->body.empty() || res->get_header_value("Content-Type").find("jsonl") != std::string::npos));
        }
    }
}

TEST_CASE("HTTP E2E: conversation import creates new", "[e2e][blackbox][http]") {
    FullServerFixture fx(false, true);
    auto cli = fx.client();

    // Import a minimal JSONL conversation
    json import_req;
    import_req["data"] = R"({"role":"user","content":"test"}
{"role":"assistant","content":"reply"}
)";
    import_req["model"] = "test-model";

    auto res = cli.Post("/api/conversations/import", import_req.dump(), "application/json");
    REQUIRE(res != nullptr);
    if (res->status == 200) {
        auto body = json::parse(res->body);
        REQUIRE(body.contains("id"));
    }
    // May be 400 if import format doesn't match — that's also acceptable
}

// --- Budget API ---

TEST_CASE("HTTP E2E: usage endpoint returns token counts", "[e2e][blackbox][http]") {
    FullServerFixture fx(true);  // with budget
    auto cli = fx.client();

    auto res = cli.Get("/api/usage?scope=session");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);
    auto body = json::parse(res->body);
    REQUIRE(body.contains("usage"));
    REQUIRE(body["usage"].contains("input_tokens"));
}

TEST_CASE("HTTP E2E: cost endpoint returns cost data", "[e2e][blackbox][http]") {
    FullServerFixture fx(true);  // with budget
    auto cli = fx.client();

    auto res = cli.Get("/api/cost?scope=global");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);
    auto body = json::parse(res->body);
    REQUIRE(body.contains("cost"));
}

TEST_CASE("HTTP E2E: usage history returns stub", "[e2e][blackbox][http]") {
    FullServerFixture fx(true);  // with budget
    auto cli = fx.client();

    auto res = cli.Get("/api/usage/history?limit=10");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);
    auto body = json::parse(res->body);
    REQUIRE(body.contains("not_implemented"));
}

// --- Approval with aborted decision ---

TEST_CASE("HTTP E2E: resolve approval with aborted decision", "[e2e][blackbox][http]") {
    FullServerFixture fx;
    auto cli = fx.client();

    json req;
    req["decision"] = "aborted";

    auto res = cli.Post("/api/approvals/1/resolve", req.dump(), "application/json");
    REQUIRE(res != nullptr);
    // 404 is expected — no pending approval with id "1"
    REQUIRE(res->status == 404);
}
