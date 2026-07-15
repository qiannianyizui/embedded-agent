#pragma once
#include "StringUtil.h"
#include "nlohmann/json.hpp"
#include <optional>

namespace ea::util {

using json = nlohmann::json;

template<typename T>
T get_or(const json& j, const std::string& key, T default_val) {
    if (j.contains(key)) {
        try { return j.at(key).get<T>(); }
        catch (...) { return default_val; }
    }
    return default_val;
}

inline std::optional<json> get_path(const json& j, const std::string& path) {
    json current = j;
    for (const auto& key : split(path, '.')) {
        if (!current.is_object() && !current.is_array()) return std::nullopt;
        if (current.is_array()) {
            try { current = current.at(std::stoi(key)); }
            catch (...) { return std::nullopt; }
        } else {
            if (!current.contains(key)) return std::nullopt;
            current = current.at(key);
        }
    }
    return current;
}

}  // namespace ea::util
