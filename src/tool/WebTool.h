#pragma once
#include "tool/ITool.h"
#include "tool/WebSearch.h"
#include <cstddef>

namespace ea::net { class HttpClient; }

namespace ea::tool {

class WebTool : public ITool {
public:
    static constexpr int kDefaultTimeoutSeconds = 30;
    static constexpr int kMaxTimeoutSeconds = 120;
    static constexpr std::size_t kMaxResponseBytes = 5 * 1024 * 1024;

    explicit WebTool(net::HttpClient* client = nullptr,
                     IWebSearchBackend* search_backend = nullptr)
        : client_(client), search_backend_(search_backend) {}

    std::string name() const override { return "web"; }
    std::string description() const override {
        return "Fetch web pages and convert HTML to text or markdown";
    }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_mutating() const override { return false; }

    // Exposed for tests — mirror the conversions opencode-dev applies.
    static std::string extract_text_from_html(const std::string& html);
    static std::string html_to_markdown(const std::string& html);

private:
    net::HttpClient* client_;
    IWebSearchBackend* search_backend_;
};

}  // namespace ea::tool
