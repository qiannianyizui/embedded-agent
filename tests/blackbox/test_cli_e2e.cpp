// tests/blackbox/test_cli_e2e.cpp
// CLI end-to-end black-box tests — launch the real embedded-agent binary
// and interact via stdin/stdout. No knowledge of internal APIs.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <string>
#include <array>
#include <memory>
#include <chrono>
#include <thread>
#include <filesystem>
#include <unistd.h>

namespace {

// Find the built binary relative to the test executable.
// CMake puts both in build/ — the binary is at ${CMAKE_BINARY_DIR}/embedded-agent.
std::string find_binary() {
    // Try environment variable first (set by CTest)
    const char* env_bin = std::getenv("EA_BINARY_PATH");
    if (env_bin && std::filesystem::exists(env_bin)) {
        return env_bin;
    }
    // Try relative to current working directory
    for (auto dir = std::filesystem::current_path(); ; dir = dir.parent_path()) {
        auto candidate = dir / "embedded-agent";
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
        if (dir == dir.parent_path()) break;  // reached root
    }
    // Try build directory
    auto build_candidate = std::filesystem::current_path().parent_path() / "build" / "embedded-agent";
    if (std::filesystem::exists(build_candidate)) {
        return build_candidate.string();
    }
    return "";
}

// Execute the binary with the given stdin input and capture stdout+stderr.
// Returns the combined output.
std::string run_cli(const std::string& input, int timeout_ms = 5000) {
    std::string binary = find_binary();
    if (binary.empty()) {
        // Binary not found — skip test gracefully
        return "__BINARY_NOT_FOUND__";
    }

    // Create a temporary file for stdin input
    std::string tmp_input = "/tmp/ea_cli_test_input_" + std::to_string(getpid());
    {
        FILE* f = fopen(tmp_input.c_str(), "w");
        if (!f) return "__PIPE_ERROR__";
        fwrite(input.data(), 1, input.size(), f);
        fclose(f);
    }

    // Run with timeout using popen
    std::string cmd = "timeout " + std::to_string(timeout_ms / 1000 + 1) +
                      " " + binary + " < " + tmp_input + " 2>&1";

    std::array<char, 4096> buffer;
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::remove(tmp_input.c_str());
        return "__PIPE_ERROR__";
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    int status = pclose(pipe);
    std::remove(tmp_input.c_str());

    // Append exit status info
    output += "\n__EXIT_CODE__=" + std::to_string(WEXITSTATUS(status));

    return output;
}

}  // anonymous namespace

// ── 1. Binary starts and prints version ────────────────────────────────────────

TEST_CASE("CLI E2E: binary starts and prints version", "[e2e][blackbox][cli]") {
    auto output = run_cli("/quit\n");
    if (output.find("__BINARY_NOT_FOUND__") != std::string::npos) {
        // Binary not available — skip
        return;
    }

    INFO("Output: " << output);
    REQUIRE(output.find("embedded-agent v0.1.0") != std::string::npos);
    REQUIRE(output.find("/quit") != std::string::npos);
}

// ── 2. /quit command exits cleanly ─────────────────────────────────────────────

TEST_CASE("CLI E2E: /quit command exits cleanly", "[e2e][blackbox][cli]") {
    auto output = run_cli("/quit\n");
    if (output.find("__BINARY_NOT_FOUND__") != std::string::npos) return;

    INFO("Output: " << output);
    // Should see "shutting down" message
    REQUIRE(output.find("shutting down") != std::string::npos);
    // Exit code should be 0
    REQUIRE(output.find("__EXIT_CODE__=0") != std::string::npos);
}

// ── 3. --help flag prints usage ────────────────────────────────────────────────

TEST_CASE("CLI E2E: --help flag prints usage", "[e2e][blackbox][cli]") {
    std::string binary = find_binary();
    if (binary.empty()) return;

    std::string cmd = binary + " --help 2>&1";
    std::array<char, 4096> buffer;
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return;

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);

    REQUIRE(output.find("Usage:") != std::string::npos);
    REQUIRE(output.find("--config") != std::string::npos);
    REQUIRE(output.find("--debug") != std::string::npos);
}

// ── 4. Invalid config path falls back to defaults ──────────────────────────────

TEST_CASE("CLI E2E: invalid config path falls back to defaults", "[e2e][blackbox][cli]") {
    std::string binary = find_binary();
    if (binary.empty()) return;

    // Pipe /quit so the process exits
    std::string cmd = "echo '/quit' | " + binary + " -c /nonexistent/path/config.json 2>&1";
    std::array<char, 4096> buffer;
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return;

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);

    // Config file not found → falls back to defaults (not a fatal error)
    REQUIRE((output.find("Config file not found") != std::string::npos ||
             output.find("config") != std::string::npos ||
             output.find("defaults") != std::string::npos));
}

// ── 5. /cost command when budget not enabled ───────────────────────────────────

TEST_CASE("CLI E2E: /cost command when budget not enabled", "[e2e][blackbox][cli]") {
    auto output = run_cli("/cost\n/quit\n");
    if (output.find("__BINARY_NOT_FOUND__") != std::string::npos) return;

    INFO("Output: " << output);
    // /cost should produce some cost-related output (either budget data or "not enabled")
    REQUIRE((output.find("Cost") != std::string::npos ||
             output.find("cost") != std::string::npos ||
             output.find("Budget") != std::string::npos ||
             output.find("budget") != std::string::npos));
}

// ── 6. /history command when no conversations ──────────────────────────────────

TEST_CASE("CLI E2E: /history command when no conversations", "[e2e][blackbox][cli]") {
    auto output = run_cli("/history\n/quit\n");
    if (output.find("__BINARY_NOT_FOUND__") != std::string::npos) return;

    INFO("Output: " << output);
    // /history should either list conversations or indicate none/persistence status
    // The output may be empty (no conversations to list) or contain a message
    // Just verify it doesn't crash and exits cleanly
    REQUIRE(output.find("shutting down") != std::string::npos);
    REQUIRE(output.find("__EXIT_CODE__=0") != std::string::npos);
}

// ── 7. Multiple commands in sequence ───────────────────────────────────────────

TEST_CASE("CLI E2E: multiple commands in sequence", "[e2e][blackbox][cli]") {
    auto output = run_cli("/cost\n/history\n/quit\n");
    if (output.find("__BINARY_NOT_FOUND__") != std::string::npos) return;

    INFO("Output: " << output);
    // Should process all commands and exit cleanly
    REQUIRE(output.find("shutting down") != std::string::npos);
    REQUIRE(output.find("__EXIT_CODE__=0") != std::string::npos);
}
