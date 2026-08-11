// test_app_builder.cpp — Unit tests for AppBuilder factory
#include <catch2/catch_test_macros.hpp>
#include "app/AppBuilder.h"
#include "app/AppContext.h"
#include "config/Config.h"
#include "io/FileSystem.h"
#include "log/Logger.h"
#include <cstdio>
#include <filesystem>

using namespace ea::app;
using namespace ea::config;

namespace {

// Helper: create a temp directory for test databases
std::string make_temp_dir() {
    auto base = std::filesystem::temp_directory_path() / "ea_app_builder_test";
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
    cfg.memory.enable_wal = false;
    cfg.conversation.path = temp_dir + "/conversations.db";
    cfg.budget.path = temp_dir + "/usage.db";
    cfg.security.autonomy = "full";
    cfg.security.approval.auto_approve_dangerous = true;
    cfg.security.approval.mode = "auto";
    cfg.agent.compression.enable = false;
    cfg.memory.strategy.type = "none";
    cfg.agent.stream = false;
    return cfg;
}

// Helper: clean up temp files
void cleanup_temp(const std::string& temp_dir) {
    std::filesystem::remove_all(temp_dir);
}

}  // anonymous namespace

TEST_CASE("AppBuilder creates AppContext with valid config", "[app]") {
    auto temp_dir = make_temp_dir();
    auto cfg = make_test_config(temp_dir);

    auto result = AppBuilder::build(cfg, false);
    REQUIRE(result.ok());

    auto& ctx = result.value();
    REQUIRE(ctx.provider != nullptr);
    REQUIRE(ctx.provider->name() == "ollama");
    REQUIRE(ctx.memory != nullptr);
    REQUIRE(ctx.security != nullptr);
    REQUIRE(ctx.registry != nullptr);
    REQUIRE(ctx.debug == false);

    cleanup_temp(temp_dir);
}

TEST_CASE("AppBuilder returns error for unknown provider type", "[app]") {
    auto temp_dir = make_temp_dir();
    auto cfg = make_test_config(temp_dir);
    cfg.provider.type = "nonexistent_provider";

    auto result = AppBuilder::build(cfg, false);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ea::ErrorCode::ConfigError);

    cleanup_temp(temp_dir);
}

TEST_CASE("AppBuilder creates budget tracker when budget is configured", "[app]") {
    auto temp_dir = make_temp_dir();
    auto cfg = make_test_config(temp_dir);
    // Enable budget by setting pricing or warn_cost
    cfg.budget.warn_cost_usd = 5.0;

    auto result = AppBuilder::build(cfg, false);
    REQUIRE(result.ok());

    auto& ctx = result.value();
    REQUIRE(ctx.budget_tracker != nullptr);
    REQUIRE(ctx.usage_store != nullptr);
    // effective_provider should be the budget tracker when budget is enabled
    REQUIRE(ctx.effective_provider != nullptr);
    REQUIRE(ctx.effective_provider == ctx.budget_tracker.get());

    cleanup_temp(temp_dir);
}

TEST_CASE("AppBuilder skips budget tracker when budget is not configured", "[app]") {
    auto temp_dir = make_temp_dir();
    auto cfg = make_test_config(temp_dir);
    // Default budget config has no pricing and warn_cost_usd = 0
    cfg.budget.pricing.clear();
    cfg.budget.warn_cost_usd = 0.0;

    auto result = AppBuilder::build(cfg, false);
    REQUIRE(result.ok());

    auto& ctx = result.value();
    REQUIRE(ctx.budget_tracker == nullptr);
    REQUIRE(ctx.usage_store == nullptr);
    // effective_provider should be the raw provider
    REQUIRE(ctx.effective_provider == ctx.provider.get());

    cleanup_temp(temp_dir);
}

TEST_CASE("AppBuilder creates compressor when compression is enabled", "[app]") {
    auto temp_dir = make_temp_dir();
    auto cfg = make_test_config(temp_dir);
    cfg.agent.compression.enable = true;
    cfg.agent.compression.max_tokens = 4000;
    cfg.agent.compression.keep_recent_turns = 2;

    auto result = AppBuilder::build(cfg, false);
    REQUIRE(result.ok());

    auto& ctx = result.value();
    REQUIRE(ctx.compressor != nullptr);
    REQUIRE(ctx.compressor->config().max_tokens == 4000);
    REQUIRE(ctx.compressor->config().keep_recent_turns == 2);

    cleanup_temp(temp_dir);
}

TEST_CASE("AppBuilder preserves debug flag", "[app]") {
    auto temp_dir = make_temp_dir();
    auto cfg = make_test_config(temp_dir);

    auto result = AppBuilder::build(cfg, true);
    REQUIRE(result.ok());
    REQUIRE(result.value().debug == true);

    auto result2 = AppBuilder::build(cfg, false);
    REQUIRE(result2.ok());
    REQUIRE(result2.value().debug == false);

    cleanup_temp(temp_dir);
}

TEST_CASE("AppBuilder registers skills tools and builds index", "[app]") {
    auto temp_dir = make_temp_dir();
    auto cfg = make_test_config(temp_dir);

    auto skills_root = temp_dir + "/skills";
    std::filesystem::create_directories(skills_root + "/development/sample");
    ea::fs::write_file(
        skills_root + "/development/sample/SKILL.md",
        "---\nname: sample-skill\ndescription: \"A test skill.\"\n---\n\n# Sample\n");
    cfg.skills.dirs.push_back(skills_root);

    auto result = AppBuilder::build(cfg, false);
    REQUIRE(result.ok());

    auto& ctx = result.value();
    REQUIRE(ctx.skills != nullptr);
    REQUIRE(ctx.skills_index.find("sample-skill") != std::string::npos);
    REQUIRE(ctx.web_search != nullptr);
    REQUIRE(ctx.web_search->name() == "duckduckgo");
    REQUIRE(ctx.registry->find("skills_list") != nullptr);
    REQUIRE(ctx.registry->find("skill_view") != nullptr);
    REQUIRE(ctx.registry->find("skill_manage") != nullptr);

    cleanup_temp(temp_dir);
}
