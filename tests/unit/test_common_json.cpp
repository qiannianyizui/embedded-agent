#include <catch2/catch_test_macros.hpp>
#include "base/JsonHelper.h"

using namespace ea::util;

TEST_CASE("get_or returns value when key exists", "[json]") {
    json j = {{"name", "test"}, {"count", 42}};
    REQUIRE(get_or(j, "name", std::string("default")) == "test");
    REQUIRE(get_or(j, "count", 0) == 42);
}

TEST_CASE("get_or returns default when key missing", "[json]") {
    json j = {{"name", "test"}};
    REQUIRE(get_or(j, "missing", std::string("default")) == "default");
}

TEST_CASE("get_or returns default on type mismatch", "[json]") {
    json j = {{"count", "not_a_number"}};
    REQUIRE(get_or(j, "count", 0) == 0);
}

TEST_CASE("get_path nested access", "[json]") {
    json j = R"({"choices":[{"message":{"content":"hello"}}]})"_json;
    auto result = get_path(j, "choices.0.message.content");
    REQUIRE(result.has_value());
    REQUIRE(result->get<std::string>() == "hello");
}

TEST_CASE("get_path returns nullopt for missing path", "[json]") {
    json j = R"({"a":1})"_json;
    REQUIRE_FALSE(get_path(j, "b.c.d").has_value());
}
