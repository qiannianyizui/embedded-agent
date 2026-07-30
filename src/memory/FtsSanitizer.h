#pragma once
// FtsSanitizer — clean and tokenize user queries for SQLite FTS5 MATCH
// FTS5 has special syntax that breaks on raw user input.
// This sanitizer: strips FTS5 operators, removes stop words, joins with OR.

#include <string>
#include <vector>

namespace ea::memory {

class FtsSanitizer {
public:
    // Sanitize a raw query for FTS5 MATCH.
    // Returns empty string if no valid tokens remain.
    static std::string sanitize(const std::string& raw_query);

    // Tokenize text into lowercase words (for Jaccard similarity)
    static std::vector<std::string> tokenize(const std::string& text);

    // Compute Jaccard similarity between two token sets
    static double jaccard_similarity(const std::vector<std::string>& a,
                                      const std::vector<std::string>& b);
};

}  // namespace ea::memory
