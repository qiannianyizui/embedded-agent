#pragma once
#include <string>
#include <functional>

namespace ea::net {

struct SseEvent {
    std::string event;
    std::string data;
    std::string id;
};

class SseParser {
public:
    void feed(const std::string& chunk, std::function<void(const SseEvent&)> on_event);
    void reset();

private:
    std::string buffer_;
};

}  // namespace ea::net
