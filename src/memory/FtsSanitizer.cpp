#include "FtsSanitizer.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace ea::memory {

// Common English stop words to strip from FTS5 queries
static const std::set<std::string>& stop_words() {
    static const std::set<std::string> words = {
        "a", "an", "the", "and", "or", "but", "in", "on", "at", "to",
        "for", "of", "with", "by", "from", "is", "it", "as", "be",
        "was", "are", "were", "been", "have", "has", "had", "do",
        "does", "did", "will", "would", "could", "should", "may",
        "might", "can", "this", "that", "these", "those", "i", "you",
        "he", "she", "we", "they", "me", "him", "her", "us", "them",
        "my", "your", "his", "its", "our", "their", "what", "which",
        "who", "when", "where", "how", "not", "no", "nor", "if",
        "then", "than", "so", "just", "about", "also", "very",
    };
    return words;
}

std::string FtsSanitizer::sanitize(const std::string& raw_query) {
    // Tokenize
    auto tokens = tokenize(raw_query);

    // Remove stop words and FTS5 special characters
    std::vector<std::string> clean;
    for (auto& tok : tokens) {
        // Skip FTS5 operators and very short tokens
        if (tok.size() < 2) continue;
        // Skip tokens that look like FTS5 syntax
        if (tok == "AND" || tok == "OR" || tok == "NOT" || tok == "NEAR") continue;
        // Skip stop words
        if (stop_words().count(tok)) continue;
        clean.push_back(std::move(tok));
    }

    if (clean.empty()) return {};

    // Join with OR for FTS5 MATCH
    std::string result;
    for (size_t i = 0; i < clean.size(); ++i) {
        if (i > 0) result += " OR ";
        result += clean[i];
    }
    return result;
}

std::vector<std::string> FtsSanitizer::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;

    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
        }
    }
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

double FtsSanitizer::jaccard_similarity(const std::vector<std::string>& a,
                                         const std::vector<std::string>& b) {
    if (a.empty() && b.empty()) return 1.0;
    if (a.empty() || b.empty()) return 0.0;

    std::set<std::string> set_a(a.begin(), a.end());
    std::set<std::string> set_b(b.begin(), b.end());

    int intersection = 0;
    for (const auto& s : set_a) {
        if (set_b.count(s)) ++intersection;
    }

    int union_size = static_cast<int>(set_a.size()) + static_cast<int>(set_b.size()) - intersection;
    return static_cast<double>(intersection) / static_cast<double>(union_size);
}

}  // namespace ea::memory
