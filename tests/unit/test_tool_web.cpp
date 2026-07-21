#include <catch2/catch_test_macros.hpp>
#include "tool/WebTool.h"

using namespace ea;
using namespace ea::tool;

TEST_CASE("WebTool name is web", "[tool][web]") {
    WebTool tool;
    REQUIRE(tool.name() == "web");
}

TEST_CASE("WebTool search action returns not implemented", "[tool][web]") {
    WebTool tool;
    auto result = tool.execute(json{{"action", "search"}, {"query", "test"}});
    REQUIRE(result.ok());
    REQUIRE(result.value().output.find("not yet implemented") != std::string::npos);
    REQUIRE(result.value().is_error == false);
}

TEST_CASE("WebTool fetch action missing url", "[tool][web]") {
    WebTool tool;
    auto result = tool.execute(json{{"action", "fetch"}});
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("WebTool missing action parameter", "[tool][web]") {
    WebTool tool;
    auto result = tool.execute(json{{"url", "http://example.com"}});
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("WebTool unknown action", "[tool][web]") {
    WebTool tool;
    auto result = tool.execute(json{{"action", "invalid"}});
    REQUIRE_FALSE(result.ok());
}
