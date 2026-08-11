#include "WebSearch.h"
#include "net/HttpClient.h"
#include "base/Types.h"
#include "log/Logger.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace ea::tool {
namespace {

std::string percent_encode(const std::string& input) {
    const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : input) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

std::string percent_decode(const std::string& input) {
    std::string out;
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            auto hex_val = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex_val(input[i + 1]);
            int lo = hex_val(input[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += input[i];
    }
    return out;
}

std::string replace_all(std::string text,
                        const std::string& from,
                        const std::string& to) {
    if (from.empty()) return text;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string strip_html(const std::string& input) {
    static const std::regex tag_re(R"(<[^>]+>)");
    auto out = std::regex_replace(input, tag_re, " ");
    out = replace_all(out, "&amp;", "&");
    out = replace_all(out, "&lt;", "<");
    out = replace_all(out, "&gt;", ">");
    out = replace_all(out, "&quot;", "\"");
    out = replace_all(out, "&#39;", "'");
    out = replace_all(out, "&nbsp;", " ");
    std::string collapsed;
    bool pending_space = false;
    for (char c : out) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            pending_space = !collapsed.empty();
        } else {
            if (pending_space) collapsed += ' ';
            pending_space = false;
            collapsed += c;
        }
    }
    return collapsed;
}

std::string extract_uddg_url(const std::string& href) {
    if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0) {
        return href;
    }
    auto pos = href.find("uddg=");
    if (pos == std::string::npos) return href;
    pos += 5;
    auto end = href.find('&', pos);
    if (end == std::string::npos) end = href.size();
    return percent_decode(href.substr(pos, end - pos));
}

Result<std::string> call_mcp(net::HttpClient* client,
                             const std::string& url,
                             const std::string& tool,
                             const json& args,
                             const std::string& api_key,
                             bool parallel_auth) {
    if (!client) return Error::tool_error("HTTP client not available");

    json body = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "tools/call"},
        {"params", {{"name", tool}, {"arguments", args}}},
    };

    net::RequestOptions opts;
    opts.timeout = std::chrono::seconds(30);
    opts.headers["Accept"] = "application/json, text/event-stream";
    if (parallel_auth && !api_key.empty()) {
        opts.headers["Authorization"] = "Bearer " + api_key;
    }

    auto result = client->post(url, opts);
    if (!result.ok()) return result.error();
    const auto& response = result.value();
    if (response.status >= 400) {
        return Error::net("MCP HTTP error " + std::to_string(response.status) +
                          ": " + response.body);
    }

    std::string text;
    std::istringstream ss(response.body);
    std::string line;
    while (std::getline(ss, line)) {
        auto t = line;
        while (!t.empty() && (t.front() == ' ' || t.front() == '\r')) t.erase(t.begin());
        if (t.rfind("data: ", 0) == 0) t = t.substr(6);
        if (t.empty() || t.front() != '{') continue;
        try {
            auto parsed = json::parse(t);
            const auto& content = parsed["result"]["content"];
            if (content.is_array()) {
                for (const auto& item : content) {
                    if (item.contains("text") && item["text"].is_string()) {
                        text = item["text"].get<std::string>();
                        break;
                    }
                }
            }
        } catch (...) {
            continue;
        }
        if (!text.empty()) break;
    }
    if (text.empty()) return Error::net("MCP response contained no text");
    return text;
}

}  // namespace

std::vector<WebSearchResult> parse_duckduckgo_html(const std::string& html) {
    std::vector<WebSearchResult> results;
    static const std::regex link_re(
        R"raw(<a[^>]*class="[^"]*result__a[^"]*"[^>]*href="([^"]*)"[^>]*>(.*?)</a>)raw",
        std::regex::icase);
    static const std::regex snippet_re(
        R"raw(<a[^>]*class="[^"]*result__snippet[^"]*"[^>]*>(.*?)</a>)raw",
        std::regex::icase);

    std::vector<std::string> urls;
    std::vector<std::string> titles;
    std::vector<std::string> snippets;
    for (std::sregex_iterator it(html.begin(), html.end(), link_re), end; it != end; ++it) {
        urls.push_back(extract_uddg_url((*it)[1].str()));
        titles.push_back(strip_html((*it)[2].str()));
    }
    for (std::sregex_iterator it(html.begin(), html.end(), snippet_re), end; it != end; ++it) {
        snippets.push_back(strip_html((*it)[1].str()));
    }

    for (size_t i = 0; i < urls.size(); ++i) {
        WebSearchResult r;
        r.title = i < titles.size() ? titles[i] : "";
        r.url = urls[i];
        r.snippet = i < snippets.size() ? snippets[i] : "";
        results.push_back(std::move(r));
    }
    return results;
}

std::vector<WebSearchResult> parse_searxng_json(const std::string& json_body) {
    std::vector<WebSearchResult> results;
    try {
        auto data = json::parse(json_body);
        if (!data.contains("results") || !data["results"].is_array()) return results;
        for (const auto& item : data["results"]) {
            WebSearchResult r;
            if (item.contains("title") && item["title"].is_string()) {
                r.title = item["title"].get<std::string>();
            }
            if (item.contains("url") && item["url"].is_string()) {
                r.url = item["url"].get<std::string>();
            }
            if (item.contains("content") && item["content"].is_string()) {
                r.snippet = item["content"].get<std::string>();
            }
            results.push_back(std::move(r));
        }
    } catch (...) {
        return {};
    }
    return results;
}

std::vector<WebSearchResult> parse_mcp_results(const std::string& text) {
    std::vector<WebSearchResult> results;
    try {
        auto data = json::parse(text);
        if (data.is_array()) {
            for (const auto& item : data) {
                WebSearchResult r;
                if (item.is_string()) {
                    r.snippet = item.get<std::string>();
                } else {
                    if (item.contains("title")) r.title = item.value("title", "");
                    if (item.contains("url")) r.url = item.value("url", "");
                    if (item.contains("content")) r.snippet = item.value("content", "");
                    if (item.contains("description")) r.snippet = item.value("description", "");
                    if (item.contains("body")) r.snippet = item.value("body", "");
                }
                results.push_back(std::move(r));
            }
            return results;
        }
        if (data.is_object() && data.contains("results") && data["results"].is_array()) {
            for (const auto& item : data["results"]) {
                WebSearchResult r;
                if (item.contains("title")) r.title = item.value("title", "");
                if (item.contains("url")) r.url = item.value("url", "");
                if (item.contains("content")) r.snippet = item.value("content", "");
                if (item.contains("description")) r.snippet = item.value("description", "");
                results.push_back(std::move(r));
            }
            return results;
        }
    } catch (...) {
        // Fall through to a single raw result.
    }
    WebSearchResult raw;
    raw.title = "Search results";
    raw.snippet = text;
    results.push_back(std::move(raw));
    return results;
}

Result<std::vector<WebSearchResult>> DuckDuckGoBackend::search(
    const std::string& query, int limit) const {
    if (!client_) return Error::tool_error("HTTP client not available");

    net::RequestOptions opts;
    opts.timeout = std::chrono::seconds(30);
    opts.headers["User-Agent"] =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36";

    std::string url = "https://html.duckduckgo.com/html/?q=" + percent_encode(query);
    auto result = client_->get(url, opts);
    if (!result.ok()) return result.error();
    if (result.value().status >= 400) {
        return Error::net("DuckDuckGo HTTP error " +
                          std::to_string(result.value().status));
    }

    auto parsed = parse_duckduckgo_html(result.value().body);
    if (parsed.size() > static_cast<size_t>(limit)) {
        parsed.resize(static_cast<size_t>(limit));
    }
    return parsed;
}

Result<std::vector<WebSearchResult>> SearXngBackend::search(
    const std::string& query, int limit) const {
    if (!client_) return Error::tool_error("HTTP client not available");
    if (base_url_.empty()) return Error::tool_error("SEARXNG_URL is not set");

    std::string base = base_url_;
    while (!base.empty() && base.back() == '/') base.pop_back();
    std::string url = base + "/search?q=" + percent_encode(query) +
                      "&format=json&pageno=1";

    net::RequestOptions opts;
    opts.timeout = std::chrono::seconds(30);
    auto result = client_->get(url, opts);
    if (!result.ok()) return result.error();
    if (result.value().status >= 400) {
        return Error::net("SearXNG HTTP error " +
                          std::to_string(result.value().status));
    }

    auto parsed = parse_searxng_json(result.value().body);
    if (parsed.size() > static_cast<size_t>(limit)) {
        parsed.resize(static_cast<size_t>(limit));
    }
    return parsed;
}

Result<std::vector<WebSearchResult>> ExaBackend::search(
    const std::string& query, int limit) const {
    std::string url = "https://mcp.exa.ai/mcp";
    if (!api_key_.empty()) {
        url += "?exaApiKey=" + percent_encode(api_key_);
    }
    json args = {
        {"query", query},
        {"type", "auto"},
        {"numResults", std::max(1, std::min(limit, 20))},
        {"livecrawl", "fallback"},
        {"contextMaxCharacters", 10000},
    };
    auto text = call_mcp(client_, url, "web_search_exa", args, "", false);
    if (!text.ok()) return text.error();
    auto parsed = parse_mcp_results(text.value());
    if (parsed.size() > static_cast<size_t>(limit)) {
        parsed.resize(static_cast<size_t>(limit));
    }
    return parsed;
}

Result<std::vector<WebSearchResult>> ParallelBackend::search(
    const std::string& query, int limit) const {
    json args = {
        {"objective", query},
        {"search_queries", json::array({query})},
    };
    auto text = call_mcp(client_, "https://search.parallel.ai/mcp",
                         "web_search", args, api_key_, true);
    if (!text.ok()) return text.error();
    auto parsed = parse_mcp_results(text.value());
    if (parsed.size() > static_cast<size_t>(limit)) {
        parsed.resize(static_cast<size_t>(limit));
    }
    return parsed;
}

std::unique_ptr<IWebSearchBackend> WebSearchFactory::create(
    const WebSearchBackendConfig& cfg, net::HttpClient* client) {
    if (cfg.backend == "searxng") {
        return std::make_unique<SearXngBackend>(client, cfg.searxng_url);
    }
    if (cfg.backend == "exa") {
        return std::make_unique<ExaBackend>(client, cfg.exa_api_key);
    }
    if (cfg.backend == "parallel") {
        return std::make_unique<ParallelBackend>(client, cfg.parallel_api_key);
    }
    if (cfg.backend != "duckduckgo") {
        EA_WARN("Unknown web search backend '{}', falling back to duckduckgo",
                cfg.backend);
    }
    return std::make_unique<DuckDuckGoBackend>(client);
}

}  // namespace ea::tool
