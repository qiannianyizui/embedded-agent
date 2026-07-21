// tests/common/MockTransport.h
#pragma once
#include "mcp/ITransport.h"
#include <queue>
#include <string>
#include <functional>

namespace ea::test {

class MockTransport : public ea::mcp::ITransport {
public:
    void enqueue_response(nlohmann::json response);
    void set_start_handler(std::function<Result<void>()> handler);
    void set_stop_handler(std::function<Result<void>()> handler);

    // Observation
    int send_count() const { return send_count_; }
    const std::vector<nlohmann::json>& sent_requests() const { return sent_reqs_; }
    nlohmann::json last_request() const;

    // ITransport interface
    Result<void> start() override;
    Result<void> stop() override;
    Result<nlohmann::json> send(const nlohmann::json& request) override;
    bool is_running() const override;

private:
    std::queue<nlohmann::json> responses_;
    std::function<Result<void>()> start_handler_;
    std::function<Result<void>()> stop_handler_;
    bool running_ = false;
    int send_count_ = 0;
    std::vector<nlohmann::json> sent_reqs_;
};

}  // namespace ea::test
