#pragma once
// FactRetriever — hybrid retrieval engine for HolographicMemory
// Phase 1: FTS5 + Jaccard similarity
// Phase 2: adds HRR vector similarity + algebraic queries

#include "memory/FactEntry.h"
#include "memory/FtsSanitizer.h"
#include "memory/EntityExtractor.h"
#include "hrr/HrrVector.h"
#include "base/Result.h"
#include <string>
#include <vector>
#include <unordered_map>

struct sqlite3;

namespace ea::memory {

struct RetrievalWeights {
    double fts = 0.6;
    double jaccard = 0.4;
    double hrr = 0.3;
};

struct ScoredFact {
    FactEntry fact;
    double fts_score = 0.0;
    double jaccard_score = 0.0;
    double hrr_score = 0.0;
    double trust_weight = 1.0;
    double final_score = 0.0;
};

class FactRetriever {
public:
    explicit FactRetriever(sqlite3* db, RetrievalWeights weights = {});

    // Search facts by query string (FTS5 + Jaccard + HRR hybrid)
    Result<std::vector<FactEntry>> search(const std::string& query,
                                           const std::string& category = "",
                                           double min_trust = 0.3,
                                           int limit = 10);

    // Find facts associated with a specific entity (via fact_entities join)
    Result<std::vector<FactEntry>> find_by_entity(const std::string& entity,
                                                    const std::string& category = "",
                                                    int limit = 10);

    // Find facts sharing entities with the given entity (neighbors)
    Result<std::vector<FactEntry>> find_related(const std::string& entity,
                                                  const std::string& category = "",
                                                  int limit = 10);

    // Find facts that share ALL specified entities (AND intersection)
    Result<std::vector<FactEntry>> find_by_entities(const std::vector<std::string>& entities,
                                                      const std::string& category = "",
                                                      int limit = 10);

    // Find potential contradictions: high entity overlap + divergent content
    Result<std::vector<ContradictionPair>> find_contradictions(
        const std::string& category = "",
        double threshold = 0.3,
        int limit = 10);

    // HRR-enabled: compute HRR similarity scores for candidates against query
    void compute_hrr_scores(std::vector<ScoredFact>& candidates,
                             const std::string& query,
                             int hrr_dim);

    // HRR-enabled: probe via HRR unbind (algebraic query)
    Result<std::vector<FactEntry>> probe_hrr(
        const std::string& entity,
        const std::string& category = "",
        int limit = 10,
        int hrr_dim = 1024);

private:
    // FTS5 candidate retrieval
    std::vector<ScoredFact> fts_candidates(const std::string& fts_query, int limit);

    // Compute Jaccard scores for candidates against query tokens
    void compute_jaccard(std::vector<ScoredFact>& candidates,
                          const std::vector<std::string>& query_tokens);

    // Apply trust weighting and sort
    void apply_trust_and_sort(std::vector<ScoredFact>& candidates,
                               double min_trust);

    // Load HRR vector for a fact from DB
    hrr::HrrVector load_hrr_vector(int fact_id, int dim);

    sqlite3* db_;
    RetrievalWeights weights_;
    EntityExtractor entity_extractor_;
};

}  // namespace ea::memory
