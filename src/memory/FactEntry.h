#pragma once
// FactEntry — structured fact data for HolographicMemory
// Mirrors Hermes Holographic's fact model with trust scoring and entity associations.

#include <string>
#include <vector>

namespace ea::memory {

struct FactEntry {
    int fact_id = 0;
    std::string content;
    std::string category;       // "user_pref", "project", "tool", "general"
    std::string tags;           // comma-separated tags
    double trust_score = 0.5;   // [0, 1], asymmetric feedback
    int retrieval_count = 0;
    int helpful_count = 0;
    std::string created_at;
    std::string updated_at;
    std::vector<std::string> entities;  // extracted entity names
};

struct FeedbackResult {
    int fact_id = 0;
    double old_trust = 0.0;
    double new_trust = 0.0;
    int helpful_count = 0;
};

struct ContradictionPair {
    FactEntry fact_a;
    FactEntry fact_b;
    double entity_overlap = 0.0;
    double content_similarity = 0.0;
    double contradiction_score = 0.0;
    std::vector<std::string> shared_entities;
};

}  // namespace ea::memory
