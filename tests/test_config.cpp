#include <catch2/catch_test_macros.hpp>
#include "config/Config.h"
#include "common/io/FileSystem.h"

using namespace ea::config;

TEST_CASE("Config loads with defaults when no file", "[config]") {
    auto cfg = load("/nonexistent/config.toml");
    REQUIRE(cfg.ok());
    REQUIRE(cfg.value().provider.type == "openai_compatible");
    REQUIRE(cfg.value().agent.max_iterations == 90);
}

TEST_CASE("Config reads TOML file", "[config]") {
    std::string tmp = "/tmp/ea_test_config.toml";
    ea::fs::write_file(tmp, R"(
[agent]
model = "test-model"
max_iterations = 42

[provider]
type = "ollama"
base_url = "http://localhost:11434"
)");

    auto cfg = load(tmp);
    REQUIRE(cfg.ok());
    REQUIRE(cfg.value().agent.model == "test-model");
    REQUIRE(cfg.value().agent.max_iterations == 42);
    REQUIRE(cfg.value().provider.type == "ollama");
    REQUIRE(cfg.value().provider.base_url == "http://localhost:11434");

    ea::fs::remove(tmp);
}
