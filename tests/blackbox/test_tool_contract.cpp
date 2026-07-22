#include <catch2/catch_test_macros.hpp>
#include "MockProvider.h"
#include "MockTool.h"
#include "TestHelpers.h"
#include "ContractTestHelper.h"
#include "tool/ShellTool.h"
#include "tool/FileTool.h"
#include "tool/SearchTool.h"
#include "tool/MemoryTool.h"
#include "memory/InMemoryBackend.h"

using namespace ea;
using namespace ea::test;
using namespace ea::test::contract;

// ── MockTool satisfies tool contract ─────────────────────────────────────────

TEST_CASE("MockTool satisfies tool contract", "[contract][greybox][tool]") {
    MockTool tool("mock_tool", "ok", true, false);
    ToolContract::verify_all(tool);
}

// ── CountingTool satisfies tool contract ─────────────────────────────────────

TEST_CASE("CountingTool satisfies tool contract", "[contract][greybox][tool]") {
    CountingTool tool("counting_tool", "counted");
    ToolContract::verify_all(tool);
}

// ── ErrorTool name is "error_tool" ───────────────────────────────────────────

TEST_CASE("ErrorTool name is error_tool", "[contract][greybox][tool]") {
    ErrorTool tool;
    REQUIRE(tool.name() == "error_tool");
}

// ── ShellTool is dangerous and mutating ──────────────────────────────────────

TEST_CASE("ShellTool is dangerous and mutating", "[contract][greybox][tool]") {
    ea::tool::ShellTool tool;
    REQUIRE(tool.is_dangerous() == true);
    REQUIRE(tool.is_mutating() == true);
}

// ── FileTool is dangerous and mutating ───────────────────────────────────────

TEST_CASE("FileTool is dangerous and mutating", "[contract][greybox][tool]") {
    ea::tool::FileTool tool;
    REQUIRE(tool.is_dangerous() == true);
    REQUIRE(tool.is_mutating() == true);
}

// ── SearchTool is non-mutating and non-dangerous ─────────────────────────────

TEST_CASE("SearchTool is non-mutating and non-dangerous", "[contract][greybox][tool]") {
    ea::tool::SearchTool tool;
    REQUIRE(tool.is_mutating() == false);
    REQUIRE(tool.is_dangerous() == false);
}

// ── MemoryTool is mutating and non-dangerous ─────────────────────────────────

TEST_CASE("MemoryTool is mutating and non-dangerous", "[contract][greybox][tool]") {
    ea::memory::InMemoryBackend backend;
    ea::tool::MemoryTool tool(&backend);
    REQUIRE(tool.is_mutating() == true);
    REQUIRE(tool.is_dangerous() == false);
}
