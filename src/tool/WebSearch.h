#pragma once
#include "base/Result.h"
#include <memory>
#include <string>
#include <vector>

namespace ea::net { class HttpClient; }

namespace ea::tool {

struct WebSearchResult {
    std::string title;
    std::string url;
    std::string snippet;
};

struct WebSearchBackendConfig {
    std::string backend = "duckduckgo";  // duckduckgo | searxng | exa | parallel
    std::string searxng_url;
    std::string exa_api_key;
    std::string parallel_api_key;
};

class IWebSearchBackend {
public:
    virtual ~IWebSearchBackend() = default;
    virtual std::string name() const = 0;
    virtual Result<std::vector<WebSearchResult>> search(
        const std::string& query, int limit) const = 0;
};

class DuckDuckGoBackend : public IWebSearchBackend {
public:
    explicit DuckDuckGoBackend(net::HttpClient* client) : client_(client) {}
    std::string name() const override { return "duckduckgo"; }
    Result<std::vector<WebSearchResult>> search(
        const std::string& query, int limit) const override;

private:
    net::HttpClient* client_;
};

class SearXngBackend : public IWebSearchBackend {
public:
    SearXngBackend(net::HttpClient* client, std::string base_url)
        : client_(client), base_url_(std::move(base_url)) {}
    std::string name() const override { return "searxng"; }
    Result<std::vector<WebSearchResult>> search(
        const std::string& query, int limit) const override;

private:
    net::HttpClient* client_;
    std::string base_url_;
};

class ExaBackend : public IWebSearchBackend {
public:
    ExaBackend(net::HttpClient* client, std::string api_key)
        : client_(client), api_key_(std::move(api_key)) {}
    std::string name() const override { return "exa"; }
    Result<std::vector<WebSearchResult>> search(
        const std::string& query, int limit) const override;

private:
    net::HttpClient* client_;
    std::string api_key_;
};

class ParallelBackend : public IWebSearchBackend {
public:
    ParallelBackend(net::HttpClient* client, std::string api_key)
        : client_(client), api_key_(std::move(api_key)) {}
    std::string name() const override { return "parallel"; }
    Result<std::vector<WebSearchResult>> search(
        const std::string& query, int limit) const override;

private:
    net::HttpClient* client_;
    std::string api_key_;
};

class WebSearchFactory {
public:
    static std::unique_ptr<IWebSearchBackend> create(
        const WebSearchBackendConfig& cfg, net::HttpClient* client);
};

// Test helpers — parsing is backend-specific and kept pure.
std::vector<WebSearchResult> parse_duckduckgo_html(const std::string& html);
std::vector<WebSearchResult> parse_searxng_json(const std::string& json_body);
std::vector<WebSearchResult> parse_mcp_results(const std::string& text);

}  // namespace ea::tool
