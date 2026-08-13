#include <catch2/catch_test_macros.hpp>
#include "config/Config.h"
#include "io/FileSystem.h"

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

TEST_CASE("Config reads skills section", "[config]") {
    std::string tmp = "/tmp/ea_test_skills_config.toml";
    ea::fs::write_file(tmp, R"(
[skills]
enable = true
dirs = ["/opt/skills", "~/skills"]
disabled = ["legacy-skill"]
template_vars = false
inline_shell = true
inline_shell_timeout = 5
plugin_mirrors = ["https://ghfast.top/", "https://gitee.com/"]

[web]
search_backend = "searxng"
searxng_url = "http://localhost:8080"
exa_api_key = "exa-secret"
parallel_api_key = "parallel-secret"
)");

    auto cfg = load(tmp);
    REQUIRE(cfg.ok());
    REQUIRE(cfg.value().skills.enable == true);
    REQUIRE(cfg.value().skills.dirs.size() == 2);
    REQUIRE(cfg.value().skills.dirs[0] == "/opt/skills");
    REQUIRE(cfg.value().skills.disabled.size() == 1);
    REQUIRE(cfg.value().skills.disabled[0] == "legacy-skill");
    REQUIRE(cfg.value().skills.template_vars == false);
    REQUIRE(cfg.value().skills.inline_shell == true);
    REQUIRE(cfg.value().skills.inline_shell_timeout == 5);
    REQUIRE(cfg.value().skills.plugin_mirrors.size() == 2);
    REQUIRE(cfg.value().skills.plugin_mirrors[0] == "https://ghfast.top/");
    REQUIRE(cfg.value().skills.plugin_mirrors[1] == "https://gitee.com/");
    REQUIRE(cfg.value().web.search_backend == "searxng");
    REQUIRE(cfg.value().web.searxng_url == "http://localhost:8080");
    REQUIRE(cfg.value().web.exa_api_key == "exa-secret");
    REQUIRE(cfg.value().web.parallel_api_key == "parallel-secret");

    ea::fs::remove(tmp);
}
