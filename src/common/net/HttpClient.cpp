#include "HttpClient.h"
#include "common/io/Logger.h"
#include <httplib.h>
#include <thread>

namespace ea::net {

static std::unique_ptr<httplib::Client> make_client(const std::string& url, const RequestOptions& opts) {
    auto client = std::make_unique<httplib::Client>(url);

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
    auto client = make_client(url, opts);

    httplib::Headers headers;
    for (auto& [k, v] : opts.headers) {
        headers.emplace(k, v);
    }

    for (int attempt = 0; attempt <= opts.retry.max_retries; ++attempt) {
        auto res = client->Get("/", headers);
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
    auto client = make_client(url, opts);

    httplib::Headers headers;
    for (auto& [k, v] : opts.headers) {
        headers.emplace(k, v);
    }

    for (int attempt = 0; attempt <= opts.retry.max_retries; ++attempt) {
        auto res = client->Post("/", headers, opts.body, "application/json");
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
    auto client = make_client(url, opts);

    httplib::Headers headers;
    for (auto& [k, v] : opts.headers) {
        headers.emplace(k, v);
    }

    auto res = client->Get("/", headers,
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
    auto client = make_client(url, opts);

    httplib::Request req;
    req.method = "POST";
    req.path = "/";
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
