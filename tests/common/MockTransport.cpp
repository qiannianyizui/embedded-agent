// tests/common/MockTransport.cpp
#include "MockTransport.h"

namespace ea::test {

void MockTransport::enqueue_response(nlohmann::json response) {
    responses_.push(std::move(response));
}

void MockTransport::set_start_handler(std::function<Result<void>()> handler) {
    start_handler_ = std::move(handler);
}

void MockTransport::set_stop_handler(std::function<Result<void>()> handler) {
    stop_handler_ = std::move(handler);
}

nlohmann::json MockTransport::last_request() const {
    return sent_reqs_.empty() ? nlohmann::json() : sent_reqs_.back();
}

Result<void> MockTransport::start() {
    if (start_handler_) return start_handler_();
    running_ = true;
    return {};
}

Result<void> MockTransport::stop() {
    if (stop_handler_) return stop_handler_();
    running_ = false;
    return {};
}

Result<nlohmann::json> MockTransport::send(const nlohmann::json& request) {
    send_count_++;
    sent_reqs_.push_back(request);
    if (!responses_.empty()) {
        auto resp = std::move(responses_.front());
        responses_.pop();
        return resp;
    }
    return Error::net("no mock responses queued");
}

bool MockTransport::is_running() const { return running_; }

}  // namespace ea::test
