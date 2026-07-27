#pragma once
#include "base/Result.h"
#include "TlsConfig.h"
#include "RetryPolicy.h"
#include <string>
#include <functional>
#include <map>
#include <chrono>

namespace ea::net {

struct HttpResponse {
    int status = 0;
    std::string body;
    std::map<std::string, std::string> headers;
};

struct RequestOptions {
    std::chrono::milliseconds timeout = std::chrono::seconds(30);
    std::map<std::string, std::string> headers;
    std::string body;
    TlsConfig tls;
    bool follow_redirects = true;
    RetryPolicy retry;
};

class HttpClient {
public:
    Result<HttpResponse> get(const std::string& url, const RequestOptions& opts = {});
    Result<HttpResponse> post(const std::string& url, const RequestOptions& opts = {});
    Result<void> stream_get(const std::string& url,
                             std::function<void(const std::string&)> on_chunk,
                             const RequestOptions& opts = {});
    Result<void> stream_post(const std::string& url,
                              std::function<void(const std::string&)> on_chunk,
                              const RequestOptions& opts = {});
};

}  // namespace ea::net
