#include <catch2/catch_test_macros.hpp>
#include "tool/FileTool.h"
#include "io/FileSystem.h"

using namespace ea;
using namespace ea::tool;

TEST_CASE("FileTool write and read roundtrip", "[tool]") {
    FileTool tool;
    std::string path = "/tmp/ea_test_file.txt";
    auto write_result = tool.execute(json{{"action", "write"}, {"path", path}, {"content", "hello world"}});
    REQUIRE(write_result.ok());
    auto read_result = tool.execute(json{{"action", "read"}, {"path", path}});
    REQUIRE(read_result.ok());
    REQUIRE(read_result.value().output.find("hello world") != std::string::npos);
    ea::fs::remove(path);
}

TEST_CASE("FileTool edit replaces string", "[tool]") {
    FileTool tool;
    std::string path = "/tmp/ea_test_edit.txt";
    tool.execute(json{{"action", "write"}, {"path", path}, {"content", "foo bar baz"}});
    auto edit_result = tool.execute(json{{"action", "edit"}, {"path", path}, {"old_string", "bar"}, {"new_string", "QUX"}});
    REQUIRE(edit_result.ok());
    auto read_result = tool.execute(json{{"action", "read"}, {"path", path}});
    REQUIRE(read_result.ok());
    REQUIRE(read_result.value().output.find("foo QUX baz") != std::string::npos);
    ea::fs::remove(path);
}
