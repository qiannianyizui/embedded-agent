#include <catch2/catch_test_macros.hpp>
#include "tool/WebTool.h"

using namespace ea;
using namespace ea::tool;

TEST_CASE("WebTool name is web", "[tool][web]") {
    WebTool tool;
    REQUIRE(tool.name() == "web");
    REQUIRE_FALSE(tool.is_mutating());
}

TEST_CASE("WebTool search action is not implemented", "[tool][web]") {
    WebTool tool;
    auto result = tool.execute(json{{"action", "search"}, {"query", "test"}});
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error);
    REQUIRE(result.value().output.find("not configured") != std::string::npos);
}

TEST_CASE("WebTool fetch requires a URL", "[tool][web]") {
    WebTool tool;
    auto result = tool.execute(json{{"action", "fetch"}});
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error);
    REQUIRE(result.value().output.find("url") != std::string::npos);

    result = tool.execute(json{{"url", ""}});
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error);
}

TEST_CASE("WebTool rejects non-http URLs", "[tool][web]") {
    WebTool tool;
    auto result = tool.execute(json{{"url", "file:///etc/passwd"}});
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error);
    REQUIRE(result.value().output.find("http:// or https://") != std::string::npos);
}

TEST_CASE("WebTool validates format and timeout", "[tool][web]") {
    WebTool tool;

    auto bad_format = tool.execute(json{{"url", "https://example.com"}, {"format", "pdf"}});
    REQUIRE(bad_format.ok());
    REQUIRE(bad_format.value().is_error);
    REQUIRE(bad_format.value().output.find("Invalid format") != std::string::npos);

    auto bad_timeout = tool.execute(json{{"url", "https://example.com"}, {"timeout", 0}});
    REQUIRE(bad_timeout.ok());
    REQUIRE(bad_timeout.value().is_error);
    REQUIRE(bad_timeout.value().output.find("Invalid timeout") != std::string::npos);

    auto huge_timeout = tool.execute(json{{"url", "https://example.com"}, {"timeout", 999}});
    REQUIRE(huge_timeout.ok());
    REQUIRE(huge_timeout.value().is_error);
}

TEST_CASE("WebTool converts HTML to plain text", "[tool][web]") {
    std::string html =
        "<html><head><style>.x{}</style></head><body>"
        "<h1>Hello</h1><script>bad()</script>"
        "<p>world <strong>wide</strong></p></body></html>";

    auto text = WebTool::extract_text_from_html(html);
    REQUIRE(text.find("Hello") != std::string::npos);
    REQUIRE(text.find("world") != std::string::npos);
    REQUIRE(text.find("wide") != std::string::npos);
    REQUIRE(text.find("bad()") == std::string::npos);
    REQUIRE(text.find(".x{}") == std::string::npos);
}

TEST_CASE("WebTool converts HTML to markdown", "[tool][web]") {
    std::string html =
        "<h1>Title</h1><p>Hello <a href=\"https://example.com\">example</a></p>"
        "<ul><li>one</li><li>two</li></ul>";

    auto md = WebTool::html_to_markdown(html);
    REQUIRE(md.find("# Title") != std::string::npos);
    REQUIRE(md.find("[example](https://example.com)") != std::string::npos);
    REQUIRE(md.find("- one") != std::string::npos);
    REQUIRE(md.find("- two") != std::string::npos);
}

TEST_CASE("WebTool markdown preserves bold and code spans", "[tool][web]") {
    std::string html = "<p><strong>bold</strong> and <code>int x = 1;</code></p>";

    auto md = WebTool::html_to_markdown(html);
    REQUIRE(md.find("**bold**") != std::string::npos);
    REQUIRE(md.find("`int x = 1;`") != std::string::npos);
}
