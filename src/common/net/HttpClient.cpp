#include "HttpClient.h"
#include "common/io/Logger.h"
#include <httplib.h>
#include <thread>

namespace ea::net {

// Split a full URL into scheme+host and path for cpp-httplib
// e.g. "https://api.example.com/v1/chat/completions"
//   -> host = "https://api.example.com", path = "/v1/chat/completions"
static bool split_url(const std::string& url, std::string& host, std::string& path) {
    // Find scheme end (://)
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return false;

    // Find path start after host
    auto path_start = url.find('/', scheme_end + 3);
    if (path_start == std::string::npos) {
        host = url;
        path = "/";
    } else {
        host = url.substr(0, path_start);
        path = url.substr(path_start);
    }
    return true;
}

static std::unique_ptr<httplib::Client> make_client(const std::string& host, const RequestOptions& opts) {
    auto client = std::make_unique<httplib::Client>(host);

    client->set_connection_timeout(opts.timeout);
    client->set_read_timeout(opts.timeout);
    client->set_follow_location(opts.follow_redirects);

    if (!opts.tls.ca_cert_path.empty()) {
        client->set_ca_cert_path(opts.tls.ca_cert_path);
    }
    client->enable_server_certificate_verification(opts.tls.verify_server);

    return client;
}

Result<HttpResponse> HttpClient::get(const std::string& url, const RequestOptions& opts) {
    std::string host, path;
    if (!split_url(url, host, path)) {
        return Error::net("invalid URL: " + url);
    }
    auto client = make_client(host, opts);

    httplib::Headers headers;
    for (auto& [k, v] : opts.headers) {
        headers.emplace(k, v);
    }

    for (int attempt = 0; attempt <= opts.retry.max_retries; ++attempt) {
        auto res = client->Get(path, headers);
        if (!res) {
            return Error::net("HTTP GET failed: " + httplib::to_string(res.error()));
        }
        if (opts.retry.should_retry(res->status)) {
            auto delay = opts.retry.delay_for(attempt);
            EA_WARN("HTTP {} received, retrying in {}ms (attempt {}/{})",
                    res->status, delay.count(), attempt + 1, opts.retry.max_retries);
            std::this_thread::sleep_for(delay);
            continue;
        }
        HttpResponse response;
        response.status = res->status;
        response.body = res->body;
        for (auto& [k, v] : res->headers) {
            response.headers[k] = v;
        }
        return response;
    }
    return Error::rate_limit("max retries exceeded");
}

Result<HttpResponse> HttpClient::post(const std::string& url, const RequestOptions& opts) {
    std::string host, path;
    if (!split_url(url, host, path)) {
        return Error::net("invalid URL: " + url);
    }
    auto client = make_client(host, opts);

    httplib::Headers headers;
    for (auto& [k, v] : opts.headers) {
        headers.emplace(k, v);
    }

    for (int attempt = 0; attempt <= opts.retry.max_retries; ++attempt) {
        auto res = client->Post(path, headers, opts.body, "application/json");
        if (!res) {
            return Error::net("HTTP POST failed: " + httplib::to_string(res.error()));
        }
        if (opts.retry.should_retry(res->status)) {
            auto delay = opts.retry.delay_for(attempt);
            EA_WARN("HTTP {} received, retrying in {}ms (attempt {}/{})",
                    res->status, delay.count(), attempt + 1, opts.retry.max_retries);
            std::this_thread::sleep_for(delay);
            continue;
        }
        HttpResponse response;
        response.status = res->status;
        response.body = res->body;
        for (auto& [k, v] : res->headers) {
            response.headers[k] = v;
        }
        return response;
    }
    return Error::rate_limit("max retries exceeded");
}

Result<void> HttpClient::stream_get(const std::string& url,
                                     std::function<void(const std::string&)> on_chunk,
                                     const RequestOptions& opts) {
    std::string host, path;
    if (!split_url(url, host, path)) {
        return Error::net("invalid URL: " + url);
    }
    auto client = make_client(host, opts);

    httplib::Headers headers;
    for (auto& [k, v] : opts.headers) {
        headers.emplace(k, v);
    }

    auto res = client->Get(path, headers,
        [&](const char* data, size_t len) -> bool {
            on_chunk(std::string(data, len));
            return true;
        });

    if (!res) {
        return Error::net("HTTP stream GET failed: " + httplib::to_string(res.error()));
    }
    if (res->status != 200) {
        return Error::net("HTTP stream GET returned " + std::to_string(res->status), res->status);
    }
    return {};
}

Result<void> HttpClient::stream_post(const std::string& url,
                                      std::function<void(const std::string&)> on_chunk,
                                      const RequestOptions& opts) {
    std::string host, path;
    if (!split_url(url, host, path)) {
        return Error::net("invalid URL: " + url);
    }
    auto client = make_client(host, opts);

    httplib::Request req;
    req.method = "POST";
    req.path = path;
    for (auto& [k, v] : opts.headers) {
        req.headers.emplace(k, v);
    }
    req.body = opts.body;
    req.content_receiver = [&on_chunk](const char* data, size_t len, uint64_t /*offset*/, uint64_t /*total*/) -> bool {
        on_chunk(std::string(data, len));
        return true;
    };

    auto res = client->send(req);
    if (!res) {
        return Error::net("HTTP stream POST failed: " + httplib::to_string(res.error()));
    }
    if (res->status != 200) {
        return Error::net("HTTP stream POST returned " + std::to_string(res->status), res->status);
    }
    return {};
}

}  // namespace ea::net
