// Scrub — credential redaction for trace payloads.
//
// Strips credential values from known patterns before writing to the
// trace log. Aligned with zeroclaw's redact::scrub_credentials():
//   api_key: sk-abc123...  →  api_key: sk-a***REDACTED***
//   Bearer abc123def...    →  Bearer abc1***REDACTED***
//   "secret": "longvalue"  →  "secret": "long***REDACTED***
//
// Applied at the rendering boundary only — never to data that flows
// back into the agent loop.
#pragma once

#include <regex>
#include <string>

namespace ea::trace {

/// Redact credential values from trace-bound text. Each matched value
/// of 8+ characters is replaced with its first 4 characters followed
/// by ***REDACTED***. Shorter values are left intact (likely not secrets).
///
/// Patterns covered:
///   - api_key / api-key / apikey  (followed by : or = and a value)
///   - Authorization: Bearer <token>
///   - JSON keys: secret / token / password / credential / private_key
inline std::string scrub_credentials(const std::string& data) {
    // Function-local statics are compiled once (thread-safe since C++11).
    //
    // Each regex captures:
    //   group 1 = prefix (key name + separator)
    //   group 2 = first 4 chars of the value (for context)
    //   The rest of the value (8+ chars total) is consumed by the match
    //   but not captured — it gets replaced by the redaction marker.
    //
    // Value pattern: [a-zA-Z0-9_+/.\-]{8,} matches base64-ish tokens,
    // API key strings, hex strings, etc. The first 4 chars are captured
    // in group 2, the remaining 4+ are consumed but not captured.

    // Use R"delim(...)delim" with a non-empty delim to allow " inside the regex.
    // The delimiter is "re" — R"re(...)re"

    // Pattern 1: api_key / api-key / apikey followed by separator and value
    //   e.g.  api_key: sk-abc123def456  →  api_key: sk-a***REDACTED***
    static const std::regex api_key_pat(
        R"re((api[_-]?key\s*[:=]\s*["']?)([a-zA-Z0-9_+/.\-]{4})[a-zA-Z0-9_+/.\-]{4,})re",
        std::regex::icase);

    // Pattern 2: Authorization: Bearer <token>
    //   e.g.  Bearer ghp_abc123def  →  Bearer ghp_***REDACTED***
    static const std::regex bearer_pat(
        R"re((Bearer\s+)([a-zA-Z0-9_+/.\-]{4})[a-zA-Z0-9_+/.\-]{4,})re",
        std::regex::icase);

    // Pattern 3: JSON keys named secret/token/password/credential/private_key
    //   e.g.  "token": "ghp_abc123def"  →  "token": "ghp_***REDACTED***
    static const std::regex json_secret_pat(
        R"re(("(?:secret|token|password|credential|private_key)"\s*:\s*")([a-zA-Z0-9_+/.\-]{4})[a-zA-Z0-9_+/.\-]{4,})re",
        std::regex::icase);

    static const std::string marker = "***REDACTED***";

    std::string result = data;
    result = std::regex_replace(result, api_key_pat, "$1$2" + marker);
    result = std::regex_replace(result, bearer_pat, "$1$2" + marker);
    result = std::regex_replace(result, json_secret_pat, "$1$2" + marker);
    return result;
}

}  // namespace ea::trace
