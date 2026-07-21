#include <catch2/catch_test_macros.hpp>
#include "tool/Toolset.h"
#include "core/ITool.h"

using namespace ea;
using namespace ea::tool;

class StubTool : public ITool {
public:
    StubTool(std::string n, bool mutating = true, bool dangerous = false)
        : name_(std::move(n)), mutating_(mutating), dangerous_(dangerous) {}

    std::string name() const override { return name_; }
    std::string description() const override { return "stub"; }
    json parameters_schema() const override { return json::object(); }
    Result<ToolResult> execute(const json&) override {
        return ToolResult{"", name_ + " result", false};
    }
    bool is_mutating() const override { return mutating_; }
    bool is_dangerous() const override { return dangerous_; }

private:
    std::string name_;
    bool mutating_;
    bool dangerous_;
};

TEST_CASE("Toolset stores tools and returns specs", "[toolset]") {
    Toolset set("core");
    set.add(std::make_unique<StubTool>("tool_a"));
    set.add(std::make_unique<StubTool>("tool_b"));

    auto specs = set.active_specs();
    REQUIRE(specs.size() == 2);
    bool found_a = false, found_b = false;
    for (const auto& s : specs) {
        if (s.name == "tool_a") found_a = true;
        if (s.name == "tool_b") found_b = true;
    }
    REQUIRE(found_a);
    REQUIRE(found_b);
}

TEST_CASE("Toolset name accessor", "[toolset]") {
    Toolset set("core");
    REQUIRE(set.name() == "core");
}

TEST_CASE("Toolset conditional availability with check_fn", "[toolset]") {
    Toolset set("web");
    bool available = true;
    set.add(std::make_unique<StubTool>("web_search"), [&available]() { return available; });
    set.add(std::make_unique<StubTool>("web_fetch"));

    auto specs = set.active_specs();
    REQUIRE(specs.size() == 2);

    available = false;
    specs = set.active_specs();
    REQUIRE(specs.size() == 1);
    REQUIRE(specs[0].name == "web_fetch");
}

TEST_CASE("Toolset execute delegates to tool", "[toolset]") {
    Toolset set("core");
    set.add(std::make_unique<StubTool>("my_tool"));

    auto result = set.execute("my_tool", json::object());
    REQUIRE(result.ok());
    REQUIRE(result.value().output == "my_tool result");
}

TEST_CASE("Toolset execute returns error for missing tool", "[toolset]") {
    Toolset set("core");
    set.add(std::make_unique<StubTool>("my_tool"));

    auto result = set.execute("nonexistent", json::object());
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("Toolset has_tool checks existence regardless of check_fn", "[toolset]") {
    Toolset set("core");
    set.add(std::make_unique<StubTool>("tool_a"));
    bool available = true;
    set.add(std::make_unique<StubTool>("tool_b"), [&available]() { return available; });

    REQUIRE(set.has_tool("tool_a"));
    REQUIRE(set.has_tool("tool_b"));
    available = false;
    REQUIRE(set.has_tool("tool_b"));
    REQUIRE_FALSE(set.has_tool("tool_c"));
}
