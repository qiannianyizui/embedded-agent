// Unit tests for Config::save() — TOML serialization roundtrip
#include <catch2/catch_test_macros.hpp>
#include "config/Config.h"
#include "io/FileSystem.h"
#include <cstdio>
#include <filesystem>

using namespace ea::config;

static std::string make_temp_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

TEST_CASE("Config::save creates a TOML file", "[config]") {
    std::string path = make_temp_path("ea_test_save.toml");
    AppConfig cfg;
    cfg.provider.type = "anthropic";
    cfg.provider.base_url = "https://api.anthropic.com";
    cfg.provider.api_key = "sk-ant-test";
    cfg.provider.default_model = "claude-sonnet-5";
    cfg.agent.model = "claude-sonnet-5";
    cfg.agent.max_iterations = 42;
    cfg.security.autonomy = "autonomous";
    cfg.security.workspace = "/tmp/test";
    cfg.config_path = path;

    auto result = save(cfg);
    REQUIRE(result.ok());

    auto exists = ea::fs::exists(path);
    REQUIRE(exists.ok());
    REQUIRE(exists.value());

    std::remove(path.c_str());
}

TEST_CASE("Config save/load roundtrip preserves key fields", "[config]") {
    std::string path = make_temp_path("ea_test_roundtrip.toml");
    AppConfig cfg;
    cfg.provider.type = "ollama";
    cfg.provider.base_url = "http://localhost:11434";
    cfg.provider.default_model = "llama3";
    cfg.agent.model = "llama3";
    cfg.agent.max_iterations = 50;
    cfg.agent.stream = false;
    cfg.security.autonomy = "supervised";
    cfg.security.workspace = "/home/user/project";
    cfg.config_path = path;

    auto save_result = save(cfg);
    REQUIRE(save_result.ok());

    auto load_result = load(path);
    REQUIRE(load_result.ok());

    auto loaded = load_result.value();
    REQUIRE(loaded.provider.type == "ollama");
    REQUIRE(loaded.provider.base_url == "http://localhost:11434");
    REQUIRE(loaded.provider.default_model == "llama3");
    REQUIRE(loaded.agent.model == "llama3");
    REQUIRE(loaded.agent.max_iterations == 50);
    REQUIRE(loaded.agent.stream == false);
    REQUIRE(loaded.security.autonomy == "supervised");
    REQUIRE(loaded.security.workspace == "/home/user/project");

    std::remove(path.c_str());
}

TEST_CASE("Config::save with budget section", "[config]") {
    std::string path = make_temp_path("ea_test_budget.toml");
    AppConfig cfg;
    cfg.config_path = path;
    cfg.budget.warn_cost_usd = 5.0;
    cfg.budget.warn_input_tokens = 50000;

    auto result = save(cfg);
    REQUIRE(result.ok());

    auto exists = ea::fs::exists(path);
    REQUIRE(exists.ok());
    REQUIRE(exists.value());

    std::remove(path.c_str());
}
