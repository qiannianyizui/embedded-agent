// Unit tests for the width-aware text wrapper
#include <catch2/catch_test_macros.hpp>
#include "tui/TextWrap.h"

using namespace ea::tui;

TEST_CASE("wrap_by_width empty input", "[tui]") {
    REQUIRE(wrap_by_width("", 10).empty());
    REQUIRE(wrap_by_width("", 0).empty());
}

TEST_CASE("wrap_by_width no width splits on newlines only", "[tui]") {
    auto lines = wrap_by_width("a\nb\nc", 0);
    REQUIRE(lines == std::vector<std::string>{"a", "b", "c"});

    lines = wrap_by_width("single line", 0);
    REQUIRE(lines == std::vector<std::string>{"single line"});
}

TEST_CASE("wrap_by_width wraps ASCII words at space boundaries", "[tui]") {
    auto lines = wrap_by_width("aa bb cc", 5);
    REQUIRE(lines == std::vector<std::string>{"aa bb", "cc"});

    lines = wrap_by_width("one two three", 8);
    REQUIRE(lines == std::vector<std::string>{"one two", "three"});

    // A word that exactly fills the line stays on it.
    lines = wrap_by_width("aa bb", 5);
    REQUIRE(lines == std::vector<std::string>{"aa bb"});
}

TEST_CASE("wrap_by_width preserves explicit newlines", "[tui]") {
    auto lines = wrap_by_width("first\nsecond third", 7);
    REQUIRE(lines == std::vector<std::string>{"first", "second", "third"});
}

TEST_CASE("wrap_by_width hard-breaks overlong words", "[tui]") {
    auto lines = wrap_by_width("abcdefgh", 4);
    REQUIRE(lines == std::vector<std::string>{"abcd", "efgh"});

    lines = wrap_by_width("abcdefgh", 3);
    REQUIRE(lines == std::vector<std::string>{"abc", "def", "gh"});

    // Overlong word after a shorter one.
    lines = wrap_by_width("ab 12345678", 4);
    REQUIRE(lines == std::vector<std::string>{"ab", "1234", "5678"});
}

TEST_CASE("wrap_by_width breaks CJK at any glyph", "[tui]") {
    auto lines = wrap_by_width("中文测试", 4);
    REQUIRE(lines == std::vector<std::string>{"中文", "测试"});

    lines = wrap_by_width("中文测试", 5);
    REQUIRE(lines == std::vector<std::string>{"中文", "测试"});

    lines = wrap_by_width("abcdef中文", 8);
    REQUIRE(lines == std::vector<std::string>{"abcdef中", "文"});
}

TEST_CASE("wrap_by_width counts wide emoji as two cells", "[tui]") {
    auto lines = wrap_by_width("😊😊", 4);
    REQUIRE(lines == std::vector<std::string>{"😊😊"});

    lines = wrap_by_width("😊😊", 2);
    REQUIRE(lines == std::vector<std::string>{"😊", "😊"});
}

TEST_CASE("wrap_by_width trims trailing spaces", "[tui]") {
    auto lines = wrap_by_width("aa bb ", 5);
    REQUIRE(lines == std::vector<std::string>{"aa bb"});

    lines = wrap_by_width("aa bb cc ", 5);
    REQUIRE(lines == std::vector<std::string>{"aa bb", "cc"});
}

TEST_CASE("wrap_by_width treats ASCII punctuation as breakable", "[tui]") {
    auto lines = wrap_by_width("a,b", 2);
    REQUIRE(lines == std::vector<std::string>{"a,", "b"});
}

TEST_CASE("wrap_by_width handles mixed CJK and ASCII", "[tui]") {
    auto lines = wrap_by_width("hello 世界", 7);
    REQUIRE(lines == std::vector<std::string>{"hello", "世界"});
}
