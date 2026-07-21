#include <catch2/catch_test_macros.hpp>
#include "tool/SearchTool.h"
#include "common/io/FileSystem.h"

using namespace ea;
using namespace ea::tool;

TEST_CASE("SearchTool name is search_files", "[tool][search]") {
    SearchTool tool;
    REQUIRE(tool.name() == "search_files");
}

TEST_CASE("SearchTool missing pattern parameter", "[tool][search]") {
    SearchTool tool;
    auto result = tool.execute(json{{"path", "/tmp"}});
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("SearchTool finds pattern in file", "[tool][search]") {
    // Create a temp file with known content
    std::string dir = "/tmp/ea_search_test";
    std::string file = dir + "/test.txt";
    ea::fs::mkdir_p(dir);
    ea::fs::write_file(file, "hello world\nfoo bar\nhello again\n");

    SearchTool tool;
    auto result = tool.execute(json{{"pattern", "hello"}, {"path", dir}});
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error == false);
    REQUIRE(result.value().output.find("hello") != std::string::npos);

    // Cleanup
    ea::fs::remove(file);
    ea::fs::remove(dir);
}

TEST_CASE("SearchTool no matches returns message", "[tool][search]") {
    std::string dir = "/tmp/ea_search_empty";
    std::string file = dir + "/test.txt";
    ea::fs::mkdir_p(dir);
    ea::fs::write_file(file, "nothing relevant here\n");

    SearchTool tool;
    auto result = tool.execute(json{{"pattern", "xyzzy_nonexistent"}, {"path", dir}});
    REQUIRE(result.ok());
    REQUIRE(result.value().output.find("No matches") != std::string::npos);

    // Cleanup
    ea::fs::remove(file);
    ea::fs::remove(dir);
}
