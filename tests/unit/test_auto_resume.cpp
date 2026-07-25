// test_auto_resume.cpp — Unit tests for auto_resume() shared logic
#include <catch2/catch_test_macros.hpp>
#include "app/auto_resume.h"
#include "app/AppBuilder.h"
#include "app/AppContext.h"
#include "agent/AgentLoop.h"
#include "config/Config.h"
#include "common/io/FileSystem.h"
#include <filesystem>

using namespace ea::app;
using namespace ea::config;

namespace {

// Helper: create a temp directory for test databases
std::string make_temp_dir() {
    auto base = std::filesystem::temp_directory_path() / "ea_auto_resume_test";
    std::filesystem::create_directories(base);
    return base.string();
}

// Helper: create a minimal valid AppConfig for testing
AppConfig make_test_config(const std::string& temp_dir) {
    AppConfig cfg;
    cfg.provider.type = "ollama";
    cfg.provider.base_url = "http://localhost:11434";
    cfg.provider.default_model = "test-model";
    cfg.memory.path = temp_dir + "/memory.db";
    cfg.memory.enable_fts5 = false;
    cfg.conversation.path = temp_dir + "/conversations.db";
    cfg.budget.path = temp_dir + "/usage.db";
    cfg.security.autonomy = "full";
    cfg.security.auto_approve_dangerous = true;
    cfg.security.approval_mode = "auto";
    cfg.agent.compression_enable = false;
    cfg.memory_strategy.type = "none";
    cfg.agent.stream = false;
    return cfg;
}

// Helper: clean up temp files
void cleanup_temp(const std::string& temp_dir) {
    std::filesystem::remove_all(temp_dir);
}

}  // anonymous namespace

TEST_CASE("auto_resume does nothing when conversation_store is null", "[app]") {
    auto temp_dir = make_temp_dir();
    auto cfg = make_test_config(temp_dir);

    auto ctx_result = AppBuilder::build(cfg);
    REQUIRE(ctx_result.ok());
    auto& ctx = ctx_result.value();
    // Force conversation_store to null
    ctx.conversation_store.reset();

    // Create a minimal AgentLoop (no callbacks needed for this test)
    ea::agent::AgentLoop loop(
        ctx.effective_provider, ctx.registry.get(), ctx.memory.get(),
        ea::agent::AgentLoop::Config{},
        [](const std::string&) {},
        nullptr);

    // Should not crash
    auto_resume(ctx, loop, true);
    REQUIRE(loop.conversation_id().empty());

    cleanup_temp(temp_dir);
}

TEST_CASE("auto_resume does nothing when auto_resume config is false", "[app]") {
    auto temp_dir = make_temp_dir();
    auto cfg = make_test_config(temp_dir);
    cfg.conversation.auto_resume = false;

    auto ctx_result = AppBuilder::build(cfg);
    REQUIRE(ctx_result.ok());
    auto& ctx = ctx_result.value();

    ea::agent::AgentLoop loop(
        ctx.effective_provider, ctx.registry.get(), ctx.memory.get(),
        ea::agent::AgentLoop::Config{},
        [](const std::string&) {},
        nullptr);

    auto_resume(ctx, loop, true);
    REQUIRE(loop.conversation_id().empty());

    cleanup_temp(temp_dir);
}

TEST_CASE("auto_resume does nothing when no conversations exist", "[app]") {
    auto temp_dir = make_temp_dir();
    auto cfg = make_test_config(temp_dir);
    cfg.conversation.auto_resume = true;

    auto ctx_result = AppBuilder::build(cfg);
    REQUIRE(ctx_result.ok());
    auto& ctx = ctx_result.value();

    ea::agent::AgentLoop loop(
        ctx.effective_provider, ctx.registry.get(), ctx.memory.get(),
        ea::agent::AgentLoop::Config{},
        [](const std::string&) {},
        nullptr);

    // No conversations have been created, so list() should return empty
    auto_resume(ctx, loop, true);
    REQUIRE(loop.conversation_id().empty());

    cleanup_temp(temp_dir);
}

TEST_CASE("auto_resume restores most recent conversation", "[app]") {
    auto temp_dir = make_temp_dir();
    auto cfg = make_test_config(temp_dir);
    cfg.conversation.auto_resume = true;

    auto ctx_result = AppBuilder::build(cfg);
    REQUIRE(ctx_result.ok());
    auto& ctx = ctx_result.value();

    // Create a conversation and add a message
    auto conv_id_result = ctx.conversation_store->create("test-model");
    REQUIRE(conv_id_result.ok());
    auto conv_id = conv_id_result.value();

    ea::Message msg;
    msg.role = ea::Role::User;
    msg.content = "Hello, world!";
    auto append_result = ctx.conversation_store->append(conv_id, msg);
    REQUIRE(append_result.ok());

    ea::agent::AgentLoop loop(
        ctx.effective_provider, ctx.registry.get(), ctx.memory.get(),
        ea::agent::AgentLoop::Config{},
        [](const std::string&) {},
        nullptr);

    auto_resume(ctx, loop, true);
    REQUIRE(loop.conversation_id() == conv_id);
    REQUIRE_FALSE(loop.history().empty());
    REQUIRE(loop.history()[0].content == "Hello, world!");

    cleanup_temp(temp_dir);
}
