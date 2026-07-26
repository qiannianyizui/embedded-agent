// tests/test_session_manager.cpp
#include <catch2/catch_test_macros.hpp>
#include "server/SessionManager.h"
#include "memory/InMemoryBackend.h"
#include "memory/ScopedMemory.h"
#include "tool/ToolRegistry.h"
#include "security/SecurityPolicy.h"
#include "provider/IProvider.h"
#include "core/Types.h"

using namespace ea;
using namespace ea::server;
using namespace ea::memory;

// Minimal mock provider for testing (unique name to avoid ODR violations
// with MockProvider in other test files)
class SessionTestProvider : public IProvider {
public:
    std::string name() const override { return "session-test"; }
    std::vector<std::string> list_models() const override { return {"session-test-model"}; }
    provider::ProviderCapabilities capabilities() const override { return {true, true, false, false, false}; }
    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        return LLMResponse{"mock response", {}, {}, "stop"};
    }
    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override { return {}; }
};

TEST_CASE("SessionManager creates session with unique ID", "[server][session]") {
    ServerConfig cfg;
    auto provider = std::make_shared<SessionTestProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();
    SessionManager mgr(cfg);

    auto* s1 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    auto* s2 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});

    REQUIRE(s1 != nullptr);
    REQUIRE(s2 != nullptr);
    REQUIRE(s1->id != s2->id);
    REQUIRE_FALSE(s1->id.empty());
}

TEST_CASE("SessionManager get returns session by ID", "[server][session]") {
    ServerConfig cfg;
    auto provider = std::make_shared<SessionTestProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();
    SessionManager mgr(cfg);

    auto* created = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    auto* found = mgr.get(created->id);

    REQUIRE(found != nullptr);
    REQUIRE(found->id == created->id);
}

TEST_CASE("SessionManager get returns nullptr for unknown ID", "[server][session]") {
    ServerConfig cfg;
    SessionManager mgr(cfg);

    REQUIRE(mgr.get("nonexistent") == nullptr);
}

TEST_CASE("SessionManager remove deletes session", "[server][session]") {
    ServerConfig cfg;
    auto provider = std::make_shared<SessionTestProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();
    SessionManager mgr(cfg);

    auto* s = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    std::string id = s->id;
    REQUIRE(mgr.remove(id));
    REQUIRE(mgr.get(id) == nullptr);
}

TEST_CASE("SessionManager remove returns false for unknown ID", "[server][session]") {
    ServerConfig cfg;
    SessionManager mgr(cfg);

    REQUIRE_FALSE(mgr.remove("nonexistent"));
}

TEST_CASE("SessionManager list returns all sessions", "[server][session]") {
    ServerConfig cfg;
    auto provider = std::make_shared<SessionTestProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();
    SessionManager mgr(cfg);

    mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});

    auto list = mgr.list();
    REQUIRE(list.size() == 2);
}

TEST_CASE("SessionManager respects max_sessions limit", "[server][session]") {
    ServerConfig cfg;
    cfg.max_sessions = 2;
    auto provider = std::make_shared<SessionTestProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();
    SessionManager mgr(cfg);

    auto* s1 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    auto* s2 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    auto* s3 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});

    REQUIRE(s1 != nullptr);
    REQUIRE(s2 != nullptr);
    REQUIRE(s3 == nullptr);  // Exceeds max_sessions
}

TEST_CASE("SessionManager cleanup_idle removes expired sessions", "[server][session]") {
    ServerConfig cfg;
    cfg.session_idle_timeout = std::chrono::seconds(0);  // Immediate timeout
    auto provider = std::make_shared<SessionTestProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();
    SessionManager mgr(cfg);

    auto* s = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    std::string id = s->id;

    // Set last_active far in the past
    s->last_active = std::chrono::steady_clock::now() - std::chrono::hours(2);

    mgr.cleanup_idle();
    REQUIRE(mgr.get(id) == nullptr);
}
