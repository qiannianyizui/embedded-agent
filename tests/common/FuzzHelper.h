// tests/common/FuzzHelper.h
#pragma once
#include "base/Types.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <random>
#include <algorithm>

namespace ea::test {

using json = nlohmann::json;

class DeterministicRng {
public:
    explicit DeterministicRng(uint64_t seed = 42) : engine_(seed) {}

    uint64_t next() { return engine_(); }

    std::string next_string(size_t min_len = 0, size_t max_len = 1024) {
        size_t len = min_len + (next() % (max_len - min_len + 1));
        static const char charset[] =
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789"
            "!@#$%^&*()_+-=[]{}|;':\",./<>?"
            "\n\r\t\x00\x01\x02\x80\xfe\xff";
        std::string result;
        result.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            result += charset[next() % (sizeof(charset) - 1)];
        }
        return result;
    }

    uint8_t next_byte() { return static_cast<uint8_t>(next() & 0xFF); }
    bool next_bool() { return next() & 1; }
    int next_int(int min, int max) { return min + static_cast<int>(next() % (max - min + 1)); }

private:
    std::mt19937_64 engine_;
};

class FuzzGenerator {
public:
    explicit FuzzGenerator(uint64_t seed = 42) : rng_(seed) {}

    std::string random_json(size_t max_depth = 5) {
        return generate_json_value(0, max_depth);
    }

    std::string random_string(size_t min_len = 0, size_t max_len = 1024) {
        return rng_.next_string(min_len, max_len);
    }

    json random_tool_call() {
        return json{
            {"id", "call_" + std::to_string(rng_.next_int(1, 9999))},
            {"type", "function"},
            {"function", {
                {"name", "tool_" + std::to_string(rng_.next_int(1, 100))},
                {"arguments", random_json(2)}
            }}
        };
    }

    std::vector<StreamChunk> random_stream_chunks(size_t count) {
        std::vector<StreamChunk> chunks;
        for (size_t i = 0; i < count; ++i) {
            StreamChunk chunk;
            chunk.type = static_cast<StreamChunk::Type>(rng_.next_int(0, 5));
            chunk.data = random_string(0, 256);
            chunks.push_back(std::move(chunk));
        }
        return chunks;
    }

    std::string random_sse_stream(size_t event_count = 10) {
        std::string result;
        for (size_t i = 0; i < event_count; ++i) {
            result += "data: " + random_string(10, 200) + "\n\n";
        }
        return result;
    }

private:
    std::string generate_json_value(size_t depth, size_t max_depth) {
        if (depth >= max_depth) {
            // Terminal values
            int choice = rng_.next_int(0, 3);
            switch (choice) {
                case 0: return "\"" + random_string(0, 50) + "\"";
                case 1: return std::to_string(rng_.next_int(-1000, 1000));
                case 2: return rng_.next_bool() ? "true" : "false";
                default: return "null";
            }
        }
        int choice = rng_.next_int(0, 5);
        switch (choice) {
            case 0: return "\"" + random_string(0, 100) + "\"";
            case 1: return std::to_string(rng_.next_int(-10000, 10000));
            case 2: return rng_.next_bool() ? "true" : "false";
            case 3: return "null";
            case 4: return generate_json_array(depth + 1, max_depth);
            case 5: return generate_json_object(depth + 1, max_depth);
        }
        return "null";
    }

    std::string generate_json_array(size_t depth, size_t max_depth) {
        size_t count = rng_.next_int(0, 5);
        std::string result = "[";
        for (size_t i = 0; i < count; ++i) {
            if (i > 0) result += ",";
            result += generate_json_value(depth, max_depth);
        }
        result += "]";
        return result;
    }

    std::string generate_json_object(size_t depth, size_t max_depth) {
        size_t count = rng_.next_int(0, 5);
        std::string result = "{";
        for (size_t i = 0; i < count; ++i) {
            if (i > 0) result += ",";
            result += "\"" + random_string(1, 20) + "\":" + generate_json_value(depth, max_depth);
        }
        result += "}";
        return result;
    }

    DeterministicRng rng_;
};

}  // namespace ea::test
