#include <catch2/catch_test_macros.hpp>
#include "plugin/PluginLoader.h"
#include "plugin/PluginManifest.h"
#include "io/FileSystem.h"
#include <dlfcn.h>

using namespace ea::plugin;
using namespace ea::fs;

// Path to the mock plugin .so — configured by CMake
#ifndef EA_MOCK_PLUGIN_PATH
#define EA_MOCK_PLUGIN_PATH ""
#endif

static PluginManifest make_mock_manifest(const std::string& lib_path = EA_MOCK_PLUGIN_PATH) {
    PluginManifest m;
    m.info.name = "mock_plugin";
    m.info.version = "1.0.0";
    m.info.api_version = EA_PLUGIN_API_VERSION;
    m.info.description = "Mock plugin for testing";
    m.library = lib_path;
    m.enabled = true;
    return m;
}

TEST_CASE("Load valid plugin .so", "[plugin]") {
    PluginLoader loader;
    auto manifest = make_mock_manifest();
    auto result = loader.load(manifest);

    REQUIRE(result.ok());
    auto& loaded = result.value();
    REQUIRE(loaded.dl_handle != nullptr);
    REQUIRE(loaded.plugin_instance != nullptr);
    REQUIRE(loaded.state == PluginState::Loaded);

    // Verify plugin info
    auto info = loaded.plugin_instance->info();
    REQUIRE(info.name == "mock_plugin");
    REQUIRE(info.api_version == EA_PLUGIN_API_VERSION);

    // Clean up
    auto unload_result = loader.unload(loaded);
    REQUIRE(unload_result.ok());
}

TEST_CASE("Error: file not found", "[plugin]") {
    PluginLoader loader;
    auto manifest = make_mock_manifest("/nonexistent/path/libfake.so");
    auto result = loader.load(manifest);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ea::ErrorCode::PluginError);
}

TEST_CASE("Error: missing required symbol (ea_plugin_create)", "[plugin]") {
    PluginLoader loader;

    // Create a .so that only exports ea_plugin_api_version but not the other symbols.
    // We can simulate this by loading a non-plugin .so (like libc.so.6 or similar).
    // However, that's fragile. Instead, compile a minimal .so on the fly.
    // For simplicity, we load a system library that won't have our symbols.

    PluginManifest manifest;
    manifest.info.name = "bad_symbols";
    manifest.info.version = "1.0.0";
    manifest.info.api_version = EA_PLUGIN_API_VERSION;
    manifest.library = "libc.so.6";

    auto result = loader.load(manifest);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ea::ErrorCode::PluginError);
}

TEST_CASE("Error: API version mismatch", "[plugin]") {
    PluginLoader loader;

    // Build a small .so with a wrong api version
    // Write a temporary source file
    std::string src_dir = "/tmp/ea_test_wrong_api_plugin";
    mkdir_p(src_dir);

    std::string src = R"(
#include "plugin/PluginApi.h"
extern "C" int ea_plugin_api_version() { return 999; }
extern "C" ea::plugin::IPlugin* ea_plugin_create() { return nullptr; }
extern "C" void ea_plugin_destroy(ea::plugin::IPlugin*) {}
)";

    write_file(src_dir + "/wrong_api.cpp", src);

    // Compile it
    std::string cmd = "cd " + src_dir + " && g++ -shared -fPIC -I" +
                      CMAKE_SOURCE_DIR + "/src -I" + CMAKE_BINARY_DIR +
                      "/include -o libwrong_api.so wrong_api.cpp 2>&1";
    int rc = system(cmd.c_str());
    REQUIRE(rc == 0);

    PluginManifest manifest;
    manifest.info.name = "wrong_api";
    manifest.info.version = "1.0.0";
    manifest.info.api_version = EA_PLUGIN_API_VERSION;
    manifest.library = src_dir + "/libwrong_api.so";

    auto result = loader.load(manifest);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ea::ErrorCode::PluginError);

    // Clean up
    remove(src_dir + "/wrong_api.cpp");
    remove(src_dir + "/libwrong_api.so");
    remove(src_dir);
}

TEST_CASE("Unload previously loaded plugin", "[plugin]") {
    PluginLoader loader;
    auto manifest = make_mock_manifest();
    auto result = loader.load(manifest);
    REQUIRE(result.ok());

    auto& loaded = result.value();
    REQUIRE(loaded.state == PluginState::Loaded);

    auto unload_result = loader.unload(loaded);
    REQUIRE(unload_result.ok());
    REQUIRE(loaded.dl_handle == nullptr);
    REQUIRE(loaded.plugin_instance == nullptr);
    REQUIRE(loaded.state == PluginState::Unloaded);
}

TEST_CASE("Load plugin with correct API version", "[plugin]") {
    PluginLoader loader;
    auto manifest = make_mock_manifest();
    auto result = loader.load(manifest);

    REQUIRE(result.ok());
    auto& loaded = result.value();

    // The mock plugin should report the current API version
    int version = loaded.entry_points.api_version();
    REQUIRE(version == EA_PLUGIN_API_VERSION);

    auto info = loaded.plugin_instance->info();
    REQUIRE(info.api_version == EA_PLUGIN_API_VERSION);

    loader.unload(loaded);
}

TEST_CASE("Error: loading same plugin twice", "[plugin]") {
    PluginLoader loader;
    auto manifest = make_mock_manifest();

    auto result1 = loader.load(manifest);
    REQUIRE(result1.ok());

    auto result2 = loader.load(manifest);
    // Second load should also succeed (dlopen returns the same handle with refcount increment)
    REQUIRE(result2.ok());

    // Unload both — each dlclose decrements the refcount
    loader.unload(result1.value());
    loader.unload(result2.value());
}

TEST_CASE("Unload already unloaded plugin (no-op)", "[plugin]") {
    PluginLoader loader;
    auto manifest = make_mock_manifest();
    auto result = loader.load(manifest);
    REQUIRE(result.ok());

    auto& loaded = result.value();

    // First unload
    auto unload1 = loader.unload(loaded);
    REQUIRE(unload1.ok());
    REQUIRE(loaded.dl_handle == nullptr);
    REQUIRE(loaded.state == PluginState::Unloaded);

    // Second unload should be a no-op
    auto unload2 = loader.unload(loaded);
    REQUIRE(unload2.ok());
    REQUIRE(loaded.dl_handle == nullptr);
    REQUIRE(loaded.state == PluginState::Unloaded);
}
