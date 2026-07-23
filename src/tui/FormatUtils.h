// FormatUtils — number/duration formatting utilities for the TUI
// Header-only; matches Hermes formatting conventions
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ea::tui {

// ---------------------------------------------------------------------------
// fmtK — compact number: 999 → "999", 1200 → "1.2K", 1.2M → "1.2M"
// ---------------------------------------------------------------------------
inline std::string fmtK(int64_t n) {
    if (n < 1000) return std::to_string(n);
    if (n < 1'000'000) {
        double v = static_cast<double>(n) / 1000.0;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1fK", v);
        // Strip trailing ".0" for whole numbers: "1.0K" → "1K"
        std::string s = buf;
        auto pos = s.find(".0K");
        if (pos != std::string::npos) s.replace(pos, 3, "K");
        return s;
    }
    if (n < 1'000'000'000) {
        double v = static_cast<double>(n) / 1'000'000.0;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1fM", v);
        std::string s = buf;
        auto pos = s.find(".0M");
        if (pos != std::string::npos) s.replace(pos, 3, "M");
        return s;
    }
    if (n < 1'000'000'000'000LL) {
        double v = static_cast<double>(n) / 1'000'000'000.0;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1fB", v);
        std::string s = buf;
        auto pos = s.find(".0B");
        if (pos != std::string::npos) s.replace(pos, 3, "B");
        return s;
    }
    double v = static_cast<double>(n) / 1'000'000'000'000.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1fT", v);
    std::string s = buf;
    auto pos = s.find(".0T");
    if (pos != std::string::npos) s.replace(pos, 3, "T");
    return s;
}

// ---------------------------------------------------------------------------
// fmtDuration — session duration: 0s, 59s, 1m 0s, 59m 59s, 1h 0m
// ---------------------------------------------------------------------------
inline std::string fmtDuration(int64_t ms) {
    int64_t t = std::max<int64_t>(0, ms / 1000);
    int64_t h = t / 3600;
    int64_t m = (t % 3600) / 60;
    int64_t s = t % 60;
    char buf[32];
    if (h > 0) {
        std::snprintf(buf, sizeof(buf), "%lldh %lldm", (long long)h, (long long)m);
    } else if (m > 0) {
        std::snprintf(buf, sizeof(buf), "%lldm %llds", (long long)m, (long long)s);
    } else {
        std::snprintf(buf, sizeof(buf), "%llds", (long long)s);
    }
    return buf;
}

// ---------------------------------------------------------------------------
// fmtElapsed — tool elapsed: 0.3s, 9.9s, 10s, 723s
// ---------------------------------------------------------------------------
inline std::string fmtElapsed(int64_t ms) {
    double sec = std::max<int64_t>(0, ms) / 1000.0;
    char buf[32];
    if (sec < 10.0) {
        std::snprintf(buf, sizeof(buf), "%.1fs", sec);
    } else {
        std::snprintf(buf, sizeof(buf), "%llds", (long long)std::round(sec));
    }
    return buf;
}

// ---------------------------------------------------------------------------
// shortModelLabel — simplify model name for display
//   "claude-sonnet-5-20250514" → "sonnet 5"
//   "anthropic/claude-opus-4-8" → "opus 4.8"
//   "gpt-4o" → "gpt 4o"
// ---------------------------------------------------------------------------
inline std::string shortModelLabel(const std::string& model) {
    if (model.empty()) return "";

    // Take last segment after '/'
    std::string name = model;
    auto slash = name.rfind('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);

    // Strip common prefixes
    const std::vector<std::string> prefixes = {"claude-", "anthropic-", "gpt-"};
    for (const auto& p : prefixes) {
        if (name.size() > p.size() && name.compare(0, p.size(), p) == 0) {
            name = name.substr(p.size());
            break;
        }
    }

    // Replace - and _ with spaces
    for (auto& c : name) {
        if (c == '-' || c == '_') c = ' ';
    }

    // Insert decimal point between digit-digit patterns like "4 8" → "4.8"
    // Only for the last digit-space-digit in the string
    for (size_t i = name.size(); i > 2; --i) {
        if (name[i-1] >= '0' && name[i-1] <= '9' &&
            name[i-2] == ' ' &&
            name[i-3] >= '0' && name[i-3] <= '9') {
            name[i-2] = '.';
        }
    }

    // Strip trailing date stamps like " 20250514"
    auto last_space = name.rfind(' ');
    if (last_space != std::string::npos && last_space + 1 < name.size()) {
        bool all_digits = true;
        for (size_t i = last_space + 1; i < name.size(); ++i) {
            if (name[i] < '0' || name[i] > '9') { all_digits = false; break; }
        }
        if (all_digits && name.size() - last_space - 1 >= 6) {
            name = name.substr(0, last_space);
        }
    }

    return name;
}

// ---------------------------------------------------------------------------
// ctxBar — context usage bar: [█████░░░░░]  (width default 10)
// ---------------------------------------------------------------------------
inline std::string ctxBar(int pct, int width = 10) {
    int p = std::max(0, std::min(100, pct));
    int filled = (p * width + 50) / 100;  // rounded
    std::string bar;
    for (int i = 0; i < filled; ++i) bar += "█";
    for (int i = filled; i < width; ++i) bar += "░";
    return bar;
}

}  // namespace ea::tui
