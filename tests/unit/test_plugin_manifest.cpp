#include <catch2/catch_test_macros.hpp>
#include "plugin/PluginManifest.h"
#include "common/io/FileSystem.h"
#include <cstdio>

using namespace ea::plugin;
using namespace ea::fs;

// Helper: create a temp dir, write plugin.toml, parse, then clean up
static std::string make_plugin_dir(const std::string& name, const std::string& toml_content) {
    std::string dir = "/tmp/ea_test_plugin_" + name;
    mkdir_p(dir);
    write_file(dir + "/plugin.toml", toml_content);
    return dir;
}

static void cleanup_plugin_dir(const std::string& dir) {
    remove(dir + "/plugin.toml");
    remove(dir);
}

TEST_CASE("Parse valid manifest with all fields", "[plugin]") {
    std::string dir = make_plugin_dir("all_fields", R"(
[plugin]
name = "weather"
version = "1.0.0"
api_version = 1
description = "Weather lookup plugin"
library = "libea_plugin_weather.so"
provides = ["tool", "event_listener"]
depends = ["geo"]
trust = "trusted"
enabled = true

[plugin.config]
api_key = "test-key"
cache_ttl = "300"
)");
    auto result = parse_manifest(dir);
    REQUIRE(result.ok());

    auto& m = result.value();
    REQUIRE(m.info.name == "weather");
    REQUIRE(m.info.version == "1.0.0");
    REQUIRE(m.info.api_version == 1);
    REQUIRE(m.info.description == "Weather lookup plugin");
    REQUIRE(m.library == "libea_plugin_weather.so");
    REQUIRE(m.info.provides.size() == 2);
    REQUIRE(m.info.provides[0] == "tool");
    REQUIRE(m.info.provides[1] == "event_listener");
    REQUIRE(m.info.depends.size() == 1);
    REQUIRE(m.info.depends[0] == "geo");
    REQUIRE(m.trust == PluginTrust::Trusted);
    REQUIRE(m.enabled == true);
    REQUIRE(m.path == dir);
    REQUIRE(m.config.size() == 2);
    REQUIRE(m.config["api_key"] == "test-key");
    REQUIRE(m.config["cache_ttl"] == "300");

    cleanup_plugin_dir(dir);
}

TEST_CASE("Parse manifest with minimal fields (defaults applied)", "[plugin]") {
    std::string dir = make_plugin_dir("minimal", R"(
[plugin]
name = "minimal"
version = "0.1.0"
api_version = 1
library = "libminimal.so"
)");
    auto result = parse_manifest(dir);
    REQUIRE(result.ok());

    auto& m = result.value();
    REQUIRE(m.info.name == "minimal");
    REQUIRE(m.info.version == "0.1.0");
    REQUIRE(m.info.api_version == 1);
    REQUIRE(m.info.description == "");
    REQUIRE(m.info.provides.empty());
    REQUIRE(m.info.depends.empty());
    REQUIRE(m.trust == PluginTrust::Untrusted);
    REQUIRE(m.enabled == true);
    REQUIRE(m.config.empty());

    cleanup_plugin_dir(dir);
}

TEST_CASE("Parse manifest with provides array", "[plugin]") {
    std::string dir = make_plugin_dir("provides", R"(
[plugin]
name = "multi"
version = "2.0.0"
api_version = 1
library = "libmulti.so"
provides = ["tool", "provider", "turn_step", "event_listener"]
)");
    auto result = parse_manifest(dir);
    REQUIRE(result.ok());

    auto& m = result.value();
    REQUIRE(m.info.provides.size() == 4);
    REQUIRE(m.info.provides[0] == "tool");
    REQUIRE(m.info.provides[1] == "provider");
    REQUIRE(m.info.provides[2] == "turn_step");
    REQUIRE(m.info.provides[3] == "event_listener");

    cleanup_plugin_dir(dir);
}

TEST_CASE("Parse manifest with depends array", "[plugin]") {
    std::string dir = make_plugin_dir("depends", R"(
[plugin]
name = "dependent"
version = "1.0.0"
api_version = 1
library = "libdep.so"
depends = ["core_utils", "logging", "auth"]
)");
    auto result = parse_manifest(dir);
    REQUIRE(result.ok());

    auto& m = result.value();
    REQUIRE(m.info.depends.size() == 3);
    REQUIRE(m.info.depends[0] == "core_utils");
    REQUIRE(m.info.depends[1] == "logging");
    REQUIRE(m.info.depends[2] == "auth");

    cleanup_plugin_dir(dir);
}

TEST_CASE("Parse manifest with trust = trusted", "[plugin]") {
    std::string dir = make_plugin_dir("trusted", R"(
[plugin]
name = "trusted_one"
version = "1.0.0"
api_version = 1
library = "libtrusted.so"
trust = "trusted"
)");
    auto result = parse_manifest(dir);
    REQUIRE(result.ok());
    REQUIRE(result.value().trust == PluginTrust::Trusted);

    cleanup_plugin_dir(dir);
}

TEST_CASE("Error: missing plugin.toml file", "[plugin]") {
    std::string dir = "/tmp/ea_test_plugin_nonexistent_" + std::to_string(reinterpret_cast<unsigned long>(&dir));
    auto result = parse_manifest(dir);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ea::ErrorCode::NotFound);
}

TEST_CASE("Error: missing required field (name)", "[plugin]") {
    std::string dir = make_plugin_dir("no_name", R"(
[plugin]
version = "1.0.0"
api_version = 1
library = "libtest.so"
)");
    auto result = parse_manifest(dir);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ea::ErrorCode::ConfigError);

    cleanup_plugin_dir(dir);
}

TEST_CASE("Error: api_version mismatch", "[plugin]") {
    std::string dir = make_plugin_dir("bad_api", R"(
[plugin]
name = "old_plugin"
version = "1.0.0"
api_version = 99
library = "libold.so"
)");
    auto result = parse_manifest(dir);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ea::ErrorCode::ConfigError);

    cleanup_plugin_dir(dir);
}

TEST_CASE("Error: invalid TOML syntax", "[plugin]") {
    std::string dir = make_plugin_dir("bad_toml", R"(
[plugin
name = "broken"
this is not valid toml
)");
    auto result = parse_manifest(dir);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ea::ErrorCode::ParseError);

    cleanup_plugin_dir(dir);
}

TEST_CASE("Parse manifest with [plugin.config] section", "[plugin]") {
    std::string dir = make_plugin_dir("config_section", R"(
[plugin]
name = "configured"
version = "1.0.0"
api_version = 1
library = "libconfig.so"

[plugin.config]
endpoint = "https://api.example.com"
timeout = "30"
retries = "3"
)");
    auto result = parse_manifest(dir);
    REQUIRE(result.ok());

    auto& m = result.value();
    REQUIRE(m.config.size() == 3);
    REQUIRE(m.config["endpoint"] == "https://api.example.com");
    REQUIRE(m.config["timeout"] == "30");
    REQUIRE(m.config["retries"] == "3");

    cleanup_plugin_dir(dir);
}
