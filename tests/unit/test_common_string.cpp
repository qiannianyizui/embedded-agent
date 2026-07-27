#include <catch2/catch_test_macros.hpp>
#include "base/StringUtil.h"

using namespace ea::util;

TEST_CASE("trim removes whitespace", "[string]") {
    REQUIRE(trim("  hello  ") == "hello");
    REQUIRE(trim("\t\nhello\r\n") == "hello");
    REQUIRE(trim("  ") == "");
    REQUIRE(trim("") == "");
}

TEST_CASE("split by delimiter", "[string]") {
    auto parts = split("a,b,c", ',');
    REQUIRE(parts.size() == 3);
    REQUIRE(parts[0] == "a");
    REQUIRE(parts[1] == "b");
    REQUIRE(parts[2] == "c");
}

TEST_CASE("split single element", "[string]") {
    auto parts = split("hello", ',');
    REQUIRE(parts.size() == 1);
    REQUIRE(parts[0] == "hello");
}

TEST_CASE("starts_with", "[string]") {
    REQUIRE(starts_with("hello world", "hello"));
    REQUIRE_FALSE(starts_with("hello world", "world"));
    REQUIRE_FALSE(starts_with("hi", "hello"));
}

TEST_CASE("ends_with", "[string]") {
    REQUIRE(ends_with("hello world", "world"));
    REQUIRE_FALSE(ends_with("hello world", "hello"));
}

TEST_CASE("to_lower", "[string]") {
    REQUIRE(to_lower("Hello WORLD") == "hello world");
}

TEST_CASE("replace_all", "[string]") {
    REQUIRE(replace_all("aabbcc", "bb", "XX") == "aaXXcc");
    REQUIRE(replace_all("aaa", "a", "bb") == "bbbbbb");
}
