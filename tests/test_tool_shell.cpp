#include <catch2/catch_test_macros.hpp>
#include "tool/ShellTool.h"

using namespace ea;
using namespace ea::tool;

TEST_CASE("ShellTool executes echo", "[tool]") {
    ShellTool tool;
    auto result = tool.execute(json{{"command", "echo hello"}});
    REQUIRE(result.ok());
    REQUIRE(result.value().output.find("hello") != std::string::npos);
}

TEST_CASE("ShellTool name is shell", "[tool]") {
    ShellTool tool;
    REQUIRE(tool.name() == "shell");
}
