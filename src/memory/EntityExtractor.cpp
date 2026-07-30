#include "EntityExtractor.h"
#include <regex>
#include <algorithm>

namespace ea::memory {

EntityExtractor::EntityExtractor(EntityExtractorConfig config)
    : config_(std::move(config)) {}

std::vector<std::string> EntityExtractor::extract(const std::string& text) const {
    std::set<std::string> entities;

    if (config_.extract_capitalized) extract_capitalized(text, entities);
    if (config_.extract_quoted) extract_quoted(text, entities);
    if (config_.extract_aka) extract_aka(text, entities);
    if (config_.extract_identifiers) extract_identifiers(text, entities);

    // Convert to vector, filter by length
    std::vector<std::string> result;
    for (const auto& e : entities) {
        if (valid_entity(e)) {
            result.push_back(e);
        }
    }
    return result;
}

void EntityExtractor::extract_capitalized(const std::string& text,
                                           std::set<std::string>& out) const {
    static const std::sregex_iterator sentinel;

    // Match 2+ consecutive capitalized words (e.g., "New York", "Python Package Index")
    static const std::regex multi_re(R"(\b([A-Z][a-z]+(?:\s+[A-Z][a-z]+)+)\b)");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), multi_re);
         it != sentinel; ++it) {
        out.insert(it->str(1));
    }

    // Single capitalized words (3+ chars) that aren't sentence starters
    static const std::regex single_re(R"((?:[^.!?]\s+|^)([A-Z][a-z]{2,})(?=\s|$|[,;:]))");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), single_re);
         it != sentinel; ++it) {
        out.insert(it->str(1));
    }
}

void EntityExtractor::extract_quoted(const std::string& text,
                                      std::set<std::string>& out) const {
    static const std::sregex_iterator sentinel;

    // Double-quoted terms
    static const std::regex dq_re(R"delim("([^"]{2,80})")delim");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), dq_re);
         it != sentinel; ++it) {
        out.insert(it->str(1));
    }

    // Single-quoted terms (skip contractions)
    static const std::regex sq_re(R"delim('([^']{2,80})')delim");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), sq_re);
         it != sentinel; ++it) {
        std::string val = it->str(1);
        if (val.find('\'') != std::string::npos) continue;
        out.insert(val);
    }
}

void EntityExtractor::extract_aka(const std::string& text,
                                   std::set<std::string>& out) const {
    static const std::sregex_iterator sentinel;

    // "also known as Y" / "aka Y"
    static const std::regex aka_re(
        R"((?:also\s+known\s+as|aka)\s+([A-Za-z0-9_\-\s]{2,40}))",
        std::regex::icase);
    for (auto it = std::sregex_iterator(text.begin(), text.end(), aka_re);
         it != sentinel; ++it) {
        std::string val = it->str(1);
        // Trim trailing whitespace/punctuation
        while (!val.empty() && (val.back() == ' ' || val.back() == ',' ||
               val.back() == '.' || val.back() == ')')) {
            val.pop_back();
        }
        if (!val.empty()) out.insert(val);
    }
}

void EntityExtractor::extract_identifiers(const std::string& text,
                                           std::set<std::string>& out) const {
    static const std::sregex_iterator sentinel;

    // CamelCase identifiers (e.g., "HolographicMemory", "FactRetriever")
    static const std::regex camel_re(R"(\b([A-Z][a-z]+[A-Z][A-Za-z]+)\b)");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), camel_re);
         it != sentinel; ++it) {
        out.insert(it->str(1));
    }

    // snake_case identifiers (e.g., "fact_retriever", "hrr_vector")
    static const std::regex snake_re(R"(\b([a-z][a-z0-9]*(?:_[a-z0-9]+){1,})\b)");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), snake_re);
         it != sentinel; ++it) {
        out.insert(it->str(1));
    }

    // kebab-case identifiers (e.g., "fact-store", "hrr-vector")
    static const std::regex kebab_re(R"(\b([a-z][a-z0-9]*(?:-[a-z0-9]+){1,})\b)");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), kebab_re);
         it != sentinel; ++it) {
        out.insert(it->str(1));
    }
}

bool EntityExtractor::valid_entity(const std::string& entity) const {
    auto len = static_cast<int>(entity.size());
    if (len < config_.min_entity_length || len > config_.max_entity_length) {
        return false;
    }
    // Skip pure numbers
    if (std::all_of(entity.begin(), entity.end(),
                    [](char c) { return std::isdigit(c) || c == '.' || c == '-'; })) {
        return false;
    }
    // Skip common stop words that might slip through
    static const std::set<std::string> stop_words = {
        "The", "This", "That", "These", "Those",
        "And", "But", "For", "Not", "With",
        "Yes", "No", "All", "Any", "Some",
    };
    if (stop_words.count(entity)) return false;
    return true;
}

}  // namespace ea::memory
