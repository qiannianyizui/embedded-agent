#include "SseParser.h"
#include "base/StringUtil.h"

namespace ea::net {

void SseParser::feed(const std::string& chunk, std::function<void(const SseEvent&)> on_event) {
    buffer_.append(chunk);

    while (true) {
        size_t pos = buffer_.find("\n\n");
        if (pos == std::string::npos) break;

        std::string event_text = buffer_.substr(0, pos);
        buffer_ = buffer_.substr(pos + 2);

        SseEvent event;
        std::string current_data;

        auto lines = ea::util::split(event_text, '\n');
        for (auto& line : lines) {
            if (ea::util::starts_with(line, ":")) continue;

            auto colon_pos = line.find(':');
            if (colon_pos == std::string::npos) continue;

            std::string field = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            if (!value.empty() && value[0] == ' ') value = value.substr(1);

            if (field == "event") {
                event.event = value;
            } else if (field == "data") {
                if (!current_data.empty()) current_data += "\n";
                current_data += value;
            } else if (field == "id") {
                event.id = value;
            }
        }

        event.data = std::move(current_data);
        if (!event.data.empty() || !event.event.empty()) {
            on_event(event);
        }
    }
}

void SseParser::reset() {
    buffer_.clear();
}

}  // namespace ea::net
