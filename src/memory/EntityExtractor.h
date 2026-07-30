#pragma once
// EntityExtractor — regex-based entity extraction from text
// Mirrors Hermes Holographic's entity extraction patterns:
//   - Capitalized phrases (2+ words)
//   - Quoted terms
//   - "aka" / "also known as" patterns
//   - Common technical identifiers (CamelCase, snake_case, kebab-case)

#include <string>
#include <vector>
#include <set>

namespace ea::memory {

struct EntityExtractorConfig {
    bool extract_capitalized = true;
    bool extract_quoted = true;
    bool extract_aka = true;
    bool extract_identifiers = true;
    int min_entity_length = 2;
    int max_entity_length = 80;
};

class EntityExtractor {
public:
    explicit EntityExtractor(EntityExtractorConfig config = EntityExtractorConfig{});

    // Extract entities from text. Returns deduplicated list.
    std::vector<std::string> extract(const std::string& text) const;

private:
    void extract_capitalized(const std::string& text, std::set<std::string>& out) const;
    void extract_quoted(const std::string& text, std::set<std::string>& out) const;
    void extract_aka(const std::string& text, std::set<std::string>& out) const;
    void extract_identifiers(const std::string& text, std::set<std::string>& out) const;

    bool valid_entity(const std::string& entity) const;

    EntityExtractorConfig config_;
};

}  // namespace ea::memory
