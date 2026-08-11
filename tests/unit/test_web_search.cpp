#include <catch2/catch_test_macros.hpp>
#include "tool/WebSearch.h"
#include "tool/WebTool.h"

using namespace ea;
using namespace ea::tool;

namespace {

class FakeBackend : public IWebSearchBackend {
public:
    std::string name() const override { return "fake"; }
    Result<std::vector<WebSearchResult>> search(
        const std::string& query, int limit) const override {
        WebSearchResult r;
        r.title = "Result for " + query;
        r.url = "https://example.com";
        r.snippet = "Snippet";
        std::vector<WebSearchResult> results(static_cast<size_t>(limit), r);
        return results;
    }
};

}  // namespace

TEST_CASE("parse_duckduckgo_html extracts results", "[tool][websearch]") {
    std::string html = R"(
<div class="result">
  <a rel="nofollow" class="result__a" href="/l/?uddg=https%3A%2F%2Fexample.com%2Fpage&amp;rut=1">Example <b>Page</b></a>
  <a class="result__snippet" href="/l/?uddg=...">A useful snippet</a>
</div>
<div class="result">
  <a rel="nofollow" class="result__a" href="https://example.org">Second</a>
  <a class="result__snippet" href="/l/?uddg=...">Another snippet</a>
</div>
)";

    auto results = parse_duckduckgo_html(html);
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].title.find("Example Page") != std::string::npos);
    REQUIRE(results[0].url == "https://example.com/page");
    REQUIRE(results[0].snippet.find("A useful snippet") != std::string::npos);
    REQUIRE(results[1].url == "https://example.org");
}

TEST_CASE("parse_searxng_json extracts results", "[tool][websearch]") {
    std::string body = R"({
        "results": [
            {"title": "First", "url": "https://a.example", "content": "Snippet A"},
            {"title": "Second", "url": "https://b.example", "content": "Snippet B"}
        ]
    })";

    auto results = parse_searxng_json(body);
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].title == "First");
    REQUIRE(results[0].url == "https://a.example");
    REQUIRE(results[0].snippet == "Snippet A");
}

TEST_CASE("parse_mcp_results handles JSON and raw text", "[tool][websearch]") {
    auto array = parse_mcp_results(R"([
        {"title": "T", "url": "https://t.example", "content": "C"}
    ])");
    REQUIRE(array.size() == 1);
    REQUIRE(array[0].title == "T");
    REQUIRE(array[0].url == "https://t.example");
    REQUIRE(array[0].snippet == "C");

    auto raw = parse_mcp_results("just some text");
    REQUIRE(raw.size() == 1);
    REQUIRE(raw[0].snippet == "just some text");
}

TEST_CASE("WebSearchFactory creates configured backends", "[tool][websearch]") {
    WebSearchBackendConfig cfg;
    cfg.backend = "duckduckgo";
    REQUIRE(WebSearchFactory::create(cfg, nullptr)->name() == "duckduckgo");

    cfg.backend = "searxng";
    cfg.searxng_url = "http://localhost:8080";
    REQUIRE(WebSearchFactory::create(cfg, nullptr)->name() == "searxng");

    cfg.backend = "exa";
    cfg.searxng_url.clear();
    REQUIRE(WebSearchFactory::create(cfg, nullptr)->name() == "exa");

    cfg.backend = "parallel";
    REQUIRE(WebSearchFactory::create(cfg, nullptr)->name() == "parallel");

    cfg.backend = "unknown";
    REQUIRE(WebSearchFactory::create(cfg, nullptr)->name() == "duckduckgo");
}

TEST_CASE("WebTool search uses pluggable backend", "[tool][web][websearch]") {
    FakeBackend backend;
    WebTool tool(nullptr, &backend);

    auto result = tool.execute(json{
        {"action", "search"}, {"query", "cpp"}, {"limit", 2}});
    REQUIRE(result.ok());
    REQUIRE_FALSE(result.value().is_error);
    REQUIRE(result.value().output.find("Result for cpp") != std::string::npos);
    REQUIRE(result.value().output.find("https://example.com") != std::string::npos);
}
