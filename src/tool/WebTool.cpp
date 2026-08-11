#include "WebTool.h"
#include "net/HttpClient.h"
#include "base/StringUtil.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace ea::tool {
namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim_copy(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
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

std::string regex_replace_all(const std::string& input,
                              const std::regex& re,
                              const std::string& fmt) {
    return std::regex_replace(input, re, fmt);
}

std::string decode_entities(const std::string& input) {
    std::string out = input;
    out = replace_all(out, "&amp;", "&");
    out = replace_all(out, "&lt;", "<");
    out = replace_all(out, "&gt;", ">");
    out = replace_all(out, "&quot;", "\"");
    out = replace_all(out, "&#39;", "'");
    out = replace_all(out, "&nbsp;", " ");
    out = replace_all(out, "&apos;", "'");
    return out;
}

std::string collapse_spaces(const std::string& input) {
    std::string out;
    bool pending_space = false;
    for (char c : input) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            pending_space = !out.empty();
        } else {
            if (pending_space) out += ' ';
            pending_space = false;
            out += c;
        }
    }
    return trim_copy(out);
}

bool is_http_url(const std::string& url) {
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return false;
    auto scheme = to_lower(url.substr(0, scheme_end));
    return scheme == "http" || scheme == "https";
}

std::string accept_header(const std::string& format) {
    if (format == "text") {
        return "text/plain;q=1.0, text/markdown;q=0.9, text/html;q=0.8, */*;q=0.1";
    }
    if (format == "html") {
        return "text/html;q=1.0, application/xhtml+xml;q=0.9, text/plain;q=0.8, "
               "text/markdown;q=0.7, */*;q=0.1";
    }
    return "text/markdown;q=1.0, text/x-markdown;q=0.9, text/plain;q=0.8, "
           "text/html;q=0.7, */*;q=0.1";
}

std::string content_type_of(const net::HttpResponse& response) {
    for (const auto& [key, value] : response.headers) {
        if (to_lower(key) == "content-type") return to_lower(value);
    }
    return "";
}

std::string strip_scripts_and_styles(const std::string& html) {
    static const std::regex script_re(
        R"(<script\b[^>]*>.*?</script\s*>)",
        std::regex::icase | std::regex::nosubs);
    static const std::regex style_re(
        R"(<style\b[^>]*>.*?</style\s*>)",
        std::regex::icase | std::regex::nosubs);
    return regex_replace_all(
        regex_replace_all(html, script_re, " "), style_re, " ");
}

}  // namespace

json WebTool::parameters_schema() const {
    return json::parse(R"({
        "type": "object",
        "properties": {
            "action": {"type": "string", "enum": ["search", "fetch"]},
            "query": {"type": "string"},
            "url": {"type": "string"},
            "limit": {"type": "integer", "minimum": 1, "maximum": 10, "default": 5},
            "format": {"type": "string", "enum": ["text", "markdown", "html"], "default": "markdown"},
            "timeout": {"type": "integer", "minimum": 1, "maximum": 120, "default": 30}
        },
        "required": []
    })");
}

std::string WebTool::extract_text_from_html(const std::string& html) {
    auto body = strip_scripts_and_styles(html);
    static const std::regex tag_re(R"(<[^>]+>)");
    body = regex_replace_all(body, tag_re, " ");
    body = decode_entities(body);
    return collapse_spaces(body);
}

std::string WebTool::html_to_markdown(const std::string& html) {
    auto body = strip_scripts_and_styles(html);

    // Links: <a href="URL">text</a> -> [text](URL)
    static const std::regex link_re(
        R"raw(<a\s+[^>]*href="([^"]*)"[^>]*>(.*?)</a>)raw",
        std::regex::icase);
    body = regex_replace_all(body, link_re, "[$2]($1)");

    // Simple ordered replacements keep the implementation readable and robust
    // enough for common pages without pulling in a full HTML parser.
    static const std::regex h1_re(R"(<h1\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex h2_re(R"(<h2\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex h3_re(R"(<h3\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex h4_re(R"(<h4\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex h5_re(R"(<h5\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex h6_re(R"(<h6\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex li_re(R"(<li\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex p_re(R"(<p\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex br_re(R"(<br\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex div_re(R"(<div\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex strong_re(R"(<strong\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex b_re(R"(<b\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex em_re(R"(<em\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex i_re(R"(<i\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex code_re(R"(<code\b[^>]*>)", std::regex::icase | std::regex::nosubs);
    static const std::regex pre_re(R"(<pre\b[^>]*>)", std::regex::icase | std::regex::nosubs);

    body = regex_replace_all(body, h1_re, "\n# ");
    body = regex_replace_all(body, h2_re, "\n## ");
    body = regex_replace_all(body, h3_re, "\n### ");
    body = regex_replace_all(body, h4_re, "\n#### ");
    body = regex_replace_all(body, h5_re, "\n##### ");
    body = regex_replace_all(body, h6_re, "\n###### ");
    body = regex_replace_all(body, li_re, "\n- ");
    body = regex_replace_all(body, p_re, "\n");
    body = regex_replace_all(body, br_re, "\n");
    body = regex_replace_all(body, div_re, "\n");
    body = regex_replace_all(body, strong_re, "**");
    body = regex_replace_all(body, b_re, "**");
    body = regex_replace_all(body, em_re, "*");
    body = regex_replace_all(body, i_re, "*");
    body = regex_replace_all(body, code_re, "`");
    body = regex_replace_all(body, pre_re, "\n```\n");

    body = replace_all(body, "</h1>", "\n");
    body = replace_all(body, "</h2>", "\n");
    body = replace_all(body, "</h3>", "\n");
    body = replace_all(body, "</h4>", "\n");
    body = replace_all(body, "</h5>", "\n");
    body = replace_all(body, "</h6>", "\n");
    body = replace_all(body, "</li>", "\n");
    body = replace_all(body, "</p>", "\n");
    body = replace_all(body, "</div>", "\n");
    body = replace_all(body, "</strong>", "**");
    body = replace_all(body, "</b>", "**");
    body = replace_all(body, "</em>", "*");
    body = replace_all(body, "</i>", "*");
    body = replace_all(body, "</code>", "`");
    body = replace_all(body, "</pre>", "\n```\n");

    static const std::regex tag_re(R"(<[^>]+>)");
    body = regex_replace_all(body, tag_re, "");
    body = decode_entities(body);

    // Normalize spaces and blank lines without mangling markdown code blocks.
    std::string out;
    bool pending_space = false;
    int newlines = 0;
    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (c == '\n') {
            ++newlines;
            if (newlines <= 2) out += c;
            pending_space = false;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (newlines == 0) pending_space = !out.empty();
        } else {
            if (pending_space && newlines == 0) out += ' ';
            pending_space = false;
            newlines = 0;
            out += c;
        }
    }
    return trim_copy(out);
}

Result<ToolResult> WebTool::execute(const json& args) {
    std::string action;
    if (args.contains("action") && args["action"].is_string()) {
        action = args["action"].get<std::string>();
        if (action == "search") {
            if (!args.contains("query") || !args["query"].is_string()) {
                return ToolResult{"", "Missing or invalid 'query' parameter", true};
            }
            if (!search_backend_) {
                return ToolResult{"", "Web search backend is not configured", true};
            }
            int limit = 5;
            if (args.contains("limit") && args["limit"].is_number_integer()) {
                limit = args["limit"].get<int>();
                if (limit <= 0 || limit > 10) {
                    return ToolResult{"", "Invalid limit: must be between 1 and 10", true};
                }
            }
            std::string query = args["query"].get<std::string>();
            auto results = search_backend_->search(query, limit);
            if (!results.ok()) {
                return ToolResult{"", results.error().message, true};
            }
            if (results.value().empty()) {
                return ToolResult{"", "No search results found", false};
            }
            std::ostringstream oss;
            for (size_t i = 0; i < results.value().size(); ++i) {
                const auto& r = results.value()[i];
                oss << (i + 1) << ". " << r.title << "\n";
                if (!r.url.empty()) oss << "   URL: " << r.url << "\n";
                if (!r.snippet.empty()) oss << "   " << r.snippet << "\n";
            }
            return ToolResult{"", oss.str(), false};
        }
        if (action != "fetch") {
            return Error::tool_error("Unknown action: " + action);
        }
    }

    if (!args.contains("url") || !args["url"].is_string()) {
        return ToolResult{"", "Missing or invalid 'url' parameter", true};
    }

    std::string url = args["url"].get<std::string>();
    if (!is_http_url(url)) {
        return ToolResult{"", "URL must use http:// or https://", true};
    }

    std::string format = "markdown";
    if (args.contains("format") && args["format"].is_string()) {
        format = args["format"].get<std::string>();
        if (format != "text" && format != "markdown" && format != "html") {
            return ToolResult{"", "Invalid format: " + format, true};
        }
    }

    int timeout = kDefaultTimeoutSeconds;
    if (args.contains("timeout") && args["timeout"].is_number_integer()) {
        timeout = args["timeout"].get<int>();
        if (timeout <= 0 || timeout > kMaxTimeoutSeconds) {
            return ToolResult{"",
                "Invalid timeout: must be between 1 and " +
                std::to_string(kMaxTimeoutSeconds) + " seconds", true};
        }
    }

    net::RequestOptions opts;
    opts.timeout = std::chrono::seconds(timeout);
    opts.headers["User-Agent"] =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36";
    opts.headers["Accept"] = accept_header(format);
    opts.headers["Accept-Language"] = "en-US,en;q=0.9";

    auto get_result = client_
        ? client_->get(url, opts)
        : net::HttpClient().get(url, opts);
    if (!get_result.ok()) {
        return ToolResult{"", get_result.error().message, true};
    }

    const auto& response = get_result.value();
    if (response.status >= 400) {
        return ToolResult{"",
            "HTTP error " + std::to_string(response.status) + ": " + response.body,
            true};
    }
    if (response.body.size() > kMaxResponseBytes) {
        return ToolResult{"", "Response too large (exceeds 5 MiB limit)", true};
    }

    std::string output = response.body;
    auto content_type = content_type_of(response);
    if (content_type.find("text/html") != std::string::npos) {
        if (format == "text") {
            output = extract_text_from_html(output);
        } else if (format == "markdown") {
            output = html_to_markdown(output);
        }
    }
    return ToolResult{"", output, false};
}

}  // namespace ea::tool
