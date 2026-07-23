// Unit tests for Spinner animation system
#include <catch2/catch_test_macros.hpp>
#include "tui/Spinner.h"
#include "tui/FormatUtils.h"
#include <chrono>
#include <string>

using namespace ea::tui;

TEST_CASE("SPINNER_VERBS has 15 entries", "[tui]") {
    REQUIRE(SPINNER_VERBS.size() == 15);
}

TEST_CASE("SPINNER_FACES has 15 entries", "[tui]") {
    REQUIRE(SPINNER_FACES.size() == 15);
}

TEST_CASE("SpinnerState start/stop", "[tui]") {
    SpinnerState state;
    REQUIRE_FALSE(state.active);
    state.start();
    REQUIRE(state.active);
    state.stop();
    REQUIRE_FALSE(state.active);
}

TEST_CASE("spinner_frame_text returns empty when inactive", "[tui]") {
    SpinnerState state;
    state.active = false;
    REQUIRE(spinner_frame_text(state).empty());
}

TEST_CASE("spinner_frame_text returns non-empty when active", "[tui]") {
    SpinnerState state;
    state.style = SpinnerStyle::Ascii;
    state.start();
    std::string frame = spinner_frame_text(state);
    REQUIRE_FALSE(frame.empty());
}

TEST_CASE("tool_verb returns correct mapping", "[tui]") {
    REQUIRE(tool_verb("shell") == "running");
    REQUIRE(tool_verb("read_file") == "reading");
    REQUIRE(tool_verb("write_file") == "writing");
    REQUIRE(tool_verb("search_code") == "searching");
}

TEST_CASE("tool_verb fallback for unknown tool", "[tui]") {
    std::string v = tool_verb("unknown_tool");
    REQUIRE_FALSE(v.empty());
}

TEST_CASE("fmtK formats numbers correctly", "[tui]") {
    REQUIRE(fmtK(0) == "0");
    REQUIRE(fmtK(999) == "999");
    REQUIRE(fmtK(1000) == "1K");
    REQUIRE(fmtK(1200) == "1.2K");
    REQUIRE(fmtK(12000) == "12K");
    REQUIRE(fmtK(1200000) == "1.2M");
}

TEST_CASE("fmtDuration formats durations correctly", "[tui]") {
    REQUIRE(fmtDuration(0) == "0s");
    REQUIRE(fmtDuration(1000) == "1s");
    REQUIRE(fmtDuration(59000) == "59s");
    REQUIRE(fmtDuration(60000) == "1m 0s");
    REQUIRE(fmtDuration(3599000) == "59m 59s");
    REQUIRE(fmtDuration(3600000) == "1h 0m");
}

TEST_CASE("fmtElapsed formats elapsed correctly", "[tui]") {
    REQUIRE(fmtElapsed(300) == "0.3s");
    REQUIRE(fmtElapsed(9900) == "9.9s");
    REQUIRE(fmtElapsed(10000) == "10s");
    REQUIRE(fmtElapsed(723000) == "723s");
}

TEST_CASE("shortModelLabel simplifies model names", "[tui]") {
    REQUIRE(shortModelLabel("claude-sonnet-5-20250514") == "sonnet 5");
    REQUIRE(shortModelLabel("gpt-4o") == "gpt 4o");
    REQUIRE(shortModelLabel("") == "");
}

TEST_CASE("ctxBar generates correct bar", "[tui]") {
    std::string bar0 = ctxBar(0, 10);
    REQUIRE(bar0.size() >= 10);  // 10 chars of ░ (each is multi-byte)
    std::string bar100 = ctxBar(100, 10);
    REQUIRE_FALSE(bar100.empty());
}

TEST_CASE("SpinnerStyle enum values", "[tui]") {
    SpinnerState state;
    state.style = SpinnerStyle::Kaomoji;
    state.start();
    REQUIRE(state.style == SpinnerStyle::Kaomoji);
    state.style = SpinnerStyle::Unicode;
    REQUIRE(state.style == SpinnerStyle::Unicode);
}
