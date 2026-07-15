#include <catch2/catch_test_macros.hpp>
#include "tool/ToolRegistry.h"

using namespace ea;
using namespace ea::tool;

// Simple mock tool for testing
class MockTool : public ITool {
public:
    MockTool(std::string n, std::string result_output)
        : name_(std::move(n)), result_output_(std::move(result_output)) {}
    std::string name() const override { return name_; }
    std::string description() const override { return "mock tool"; }
    json parameters_schema() const override { return json::object(); }
    Result<ToolResult> execute(const json& /*args*/) override {
        return ToolResult{"", result_output_, false};
    }
private:
    std::string name_;
    std::string result_output_;
};

TEST_CASE("ToolRegistry register and find", "[tool]") {
    ToolRegistry registry;
    registry.register_tool(std::make_unique<MockTool>("test_tool", "hello"));
    REQUIRE(registry.find("test_tool") != nullptr);
    REQUIRE(registry.find("nonexistent") == nullptr);
}

TEST_CASE("ToolRegistry get_all_specs", "[tool]") {
    ToolRegistry registry;
    registry.register_tool(std::make_unique<MockTool>("tool_a", "a"));
    registry.register_tool(std::make_unique<MockTool>("tool_b", "b"));
    auto specs = registry.get_all_specs();
    REQUIRE(specs.size() == 2);
}

TEST_CASE("ToolRegistry execute existing tool", "[tool]") {
    ToolRegistry registry;
    registry.register_tool(std::make_unique<MockTool>("test", "output"));
    auto result = registry.execute("test", json::object());
    REQUIRE(result.ok());
    REQUIRE(result.value().output == "output");
}

TEST_CASE("ToolRegistry execute nonexistent tool", "[tool]") {
    ToolRegistry registry;
    auto result = registry.execute("missing", json::object());
    REQUIRE_FALSE(result.ok());
}
