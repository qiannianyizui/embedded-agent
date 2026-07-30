#include "FactRetriever.h"
#include "log/Logger.h"
#include <sqlite3.h>
#include <algorithm>
#include <cmath>
#include <sstream>

#ifdef EA_ENABLE_HRR
#include "hrr/HrrVector.h"
#endif

namespace ea::memory {

FactRetriever::FactRetriever(sqlite3* db, RetrievalWeights weights)
    : db_(db), weights_(std::move(weights)) {}

Result<std::vector<FactEntry>> FactRetriever::search(const std::string& query,
                                                       const std::string& category,
                                                       double min_trust,
                                                       int limit) {
    if (!db_) return Error::db("database not open");

    // Sanitize query for FTS5
    std::string fts_query = FtsSanitizer::sanitize(query);
    auto query_tokens = FtsSanitizer::tokenize(query);

    // Get FTS5 candidates
    auto candidates = fts_candidates(fts_query, limit * 3);

    // If FTS5 returned nothing, try LIKE fallback
    if (candidates.empty() && !query.empty()) {
        const char* like_sql = R"(
            SELECT f.fact_id, f.content, f.category, f.tags,
                   f.trust_score, f.retrieval_count, f.helpful_count,
                   f.created_at, f.updated_at
            FROM facts f
            WHERE f.content LIKE ? AND f.trust_score >= ?
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, like_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string pattern = "%" + query + "%";
            sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(stmt, 2, min_trust);
            if (!category.empty()) {
                // Append category filter
                sqlite3_finalize(stmt);
                std::string cat_sql = std::string(like_sql) + " AND f.category = ?";
                sqlite3_prepare_v2(db_, cat_sql.c_str(), -1, &stmt, nullptr);
                sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(stmt, 2, min_trust);
                sqlite3_bind_text(stmt, 3, category.c_str(), -1, SQLITE_TRANSIENT);
            }

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                ScoredFact sf;
                sf.fact.fact_id = sqlite3_column_int(stmt, 0);
                sf.fact.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                sf.fact.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
                    sf.fact.tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                sf.fact.trust_score = sqlite3_column_double(stmt, 4);
                sf.fact.retrieval_count = sqlite3_column_int(stmt, 5);
                sf.fact.helpful_count = sqlite3_column_int(stmt, 6);
                sf.fact.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
                if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
                    sf.fact.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
                sf.fts_score = 0.5;  // moderate score for LIKE matches
                candidates.push_back(std::move(sf));
            }
            sqlite3_finalize(stmt);
        }
    }

    // Compute Jaccard scores
    compute_jaccard(candidates, query_tokens);

    // Apply trust weighting and sort
    apply_trust_and_sort(candidates, min_trust);

    // Category filter
    if (!category.empty()) {
        candidates.erase(
            std::remove_if(candidates.begin(), candidates.end(),
                [&](const ScoredFact& sf) { return sf.fact.category != category; }),
            candidates.end());
    }

    // Compute HRR scores (if enabled via weights)
    if (weights_.hrr > 0.0) {
        compute_hrr_scores(candidates, query, 1024);
    }

    // Return top-N
    std::vector<FactEntry> results;
    int n = std::min(limit, static_cast<int>(candidates.size()));
    for (int i = 0; i < n; ++i) {
        results.push_back(std::move(candidates[i].fact));
    }
    return results;
}

Result<std::vector<FactEntry>> FactRetriever::find_by_entity(
    const std::string& entity, const std::string& category, int limit) {
    if (!db_) return Error::db("database not open");

    const char* sql = R"(
        SELECT f.fact_id, f.content, f.category, f.tags,
               f.trust_score, f.retrieval_count, f.helpful_count,
               f.created_at, f.updated_at
        FROM facts f
        JOIN fact_entities fe ON f.fact_id = fe.fact_id
        JOIN entities e ON fe.entity_id = e.entity_id
        WHERE e.name = ? AND f.trust_score >= 0.3
    )";

    std::string full_sql = sql;
    if (!category.empty()) {
        full_sql += " AND f.category = ?";
    }
    full_sql += " ORDER BY f.trust_score DESC LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, full_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, entity.c_str(), -1, SQLITE_TRANSIENT);
    int bind_idx = 2;
    if (!category.empty()) {
        sqlite3_bind_text(stmt, bind_idx++, category.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, bind_idx, limit);

    std::vector<FactEntry> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FactEntry fe;
        fe.fact_id = sqlite3_column_int(stmt, 0);
        fe.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        fe.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            fe.tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        fe.trust_score = sqlite3_column_double(stmt, 4);
        fe.retrieval_count = sqlite3_column_int(stmt, 5);
        fe.helpful_count = sqlite3_column_int(stmt, 6);
        fe.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
            fe.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        results.push_back(std::move(fe));
    }
    sqlite3_finalize(stmt);
    return results;
}

Result<std::vector<FactEntry>> FactRetriever::find_related(
    const std::string& entity, const std::string& category, int limit) {
    if (!db_) return Error::db("database not open");

    // Find facts that share entities with the target entity's facts
    const char* sql = R"(
        SELECT DISTINCT f2.fact_id, f2.content, f2.category, f2.tags,
               f2.trust_score, f2.retrieval_count, f2.helpful_count,
               f2.created_at, f2.updated_at
        FROM facts f1
        JOIN fact_entities fe1 ON f1.fact_id = fe1.fact_id
        JOIN entities e1 ON fe1.entity_id = e1.entity_id
        JOIN fact_entities fe2 ON fe1.entity_id = fe2.entity_id AND fe2.fact_id != f1.fact_id
        JOIN facts f2 ON fe2.fact_id = f2.fact_id
        WHERE e1.name = ? AND f2.trust_score >= 0.3
    )";

    std::string full_sql = sql;
    if (!category.empty()) {
        full_sql += " AND f2.category = ?";
    }
    full_sql += " ORDER BY f2.trust_score DESC LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, full_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, entity.c_str(), -1, SQLITE_TRANSIENT);
    int bind_idx = 2;
    if (!category.empty()) {
        sqlite3_bind_text(stmt, bind_idx++, category.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, bind_idx, limit);

    std::vector<FactEntry> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FactEntry fe;
        fe.fact_id = sqlite3_column_int(stmt, 0);
        fe.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        fe.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            fe.tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        fe.trust_score = sqlite3_column_double(stmt, 4);
        fe.retrieval_count = sqlite3_column_int(stmt, 5);
        fe.helpful_count = sqlite3_column_int(stmt, 6);
        fe.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
            fe.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        results.push_back(std::move(fe));
    }
    sqlite3_finalize(stmt);
    return results;
}

Result<std::vector<FactEntry>> FactRetriever::find_by_entities(
    const std::vector<std::string>& entities, const std::string& category, int limit) {
    if (!db_) return Error::db("database not open");
    if (entities.empty()) return std::vector<FactEntry>{};

    // Find facts that have ALL specified entities (AND intersection)
    // Using GROUP BY + HAVING COUNT = N
    std::string placeholders;
    for (size_t i = 0; i < entities.size(); ++i) {
        if (i > 0) placeholders += ", ";
        placeholders += "?";
    }

    std::string sql = R"(
        SELECT f.fact_id, f.content, f.category, f.tags,
               f.trust_score, f.retrieval_count, f.helpful_count,
               f.created_at, f.updated_at
        FROM facts f
        JOIN fact_entities fe ON f.fact_id = fe.fact_id
        JOIN entities e ON fe.entity_id = e.entity_id
        WHERE e.name IN ()" + placeholders + R"()
        AND f.trust_score >= 0.3
    )";

    if (!category.empty()) {
        sql += " AND f.category = ?";
    }

    sql += " GROUP BY f.fact_id HAVING COUNT(DISTINCT e.entity_id) = "
         + std::to_string(entities.size())
         + " ORDER BY f.trust_score DESC LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    int bind_idx = 1;
    for (const auto& entity : entities) {
        sqlite3_bind_text(stmt, bind_idx++, entity.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (!category.empty()) {
        sqlite3_bind_text(stmt, bind_idx++, category.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, bind_idx, limit);

    std::vector<FactEntry> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FactEntry fe;
        fe.fact_id = sqlite3_column_int(stmt, 0);
        fe.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        fe.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            fe.tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        fe.trust_score = sqlite3_column_double(stmt, 4);
        fe.retrieval_count = sqlite3_column_int(stmt, 5);
        fe.helpful_count = sqlite3_column_int(stmt, 6);
        fe.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
            fe.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        results.push_back(std::move(fe));
    }
    sqlite3_finalize(stmt);
    return results;
}

Result<std::vector<ContradictionPair>> FactRetriever::find_contradictions(
    const std::string& category, double threshold, int limit) {
    if (!db_) return Error::db("database not open");

    // Find pairs of facts that share entities but have different content
    // This is a simplified version — full contradiction detection would use
    // HRR similarity in Phase 2
    const char* sql = R"(
        SELECT f1.fact_id, f1.content, f1.category, f1.trust_score,
               f2.fact_id, f2.content, f2.category, f2.trust_score,
               GROUP_CONCAT(DISTINCT e.name) as shared_entities,
               COUNT(DISTINCT e.entity_id) as overlap_count
        FROM facts f1
        JOIN fact_entities fe1 ON f1.fact_id = fe1.fact_id
        JOIN fact_entities fe2 ON fe1.entity_id = fe2.entity_id AND fe2.fact_id > f1.fact_id
        JOIN facts f2 ON fe2.fact_id = f2.fact_id
        JOIN entities e ON fe1.entity_id = e.entity_id
        WHERE f1.trust_score >= 0.3 AND f2.trust_score >= 0.3
    )";

    std::string full_sql = sql;
    if (!category.empty()) {
        full_sql += " AND f1.category = ? AND f2.category = ?";
    }
    full_sql += R"(
        GROUP BY f1.fact_id, f2.fact_id
        HAVING overlap_count >= 1
        LIMIT ?
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, full_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    int bind_idx = 1;
    if (!category.empty()) {
        sqlite3_bind_text(stmt, bind_idx++, category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bind_idx++, category.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, bind_idx, limit);

    std::vector<ContradictionPair> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ContradictionPair pair;
        pair.fact_a.fact_id = sqlite3_column_int(stmt, 0);
        pair.fact_a.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        pair.fact_a.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        pair.fact_a.trust_score = sqlite3_column_double(stmt, 3);
        pair.fact_b.fact_id = sqlite3_column_int(stmt, 4);
        pair.fact_b.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        pair.fact_b.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        pair.fact_b.trust_score = sqlite3_column_double(stmt, 7);

        // Parse shared entities from GROUP_CONCAT
        std::string shared = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        std::istringstream iss(shared);
        std::string entity;
        while (std::getline(iss, entity, ',')) {
            if (!entity.empty()) {
                pair.shared_entities.push_back(entity);
            }
        }

        pair.entity_overlap = static_cast<double>(sqlite3_column_int(stmt, 9));

        // Compute Jaccard content similarity
        auto tok_a = FtsSanitizer::tokenize(pair.fact_a.content);
        auto tok_b = FtsSanitizer::tokenize(pair.fact_b.content);
        pair.content_similarity = FtsSanitizer::jaccard_similarity(tok_a, tok_b);

        // Contradiction score: high entity overlap + low content similarity
        pair.contradiction_score = pair.entity_overlap * (1.0 - pair.content_similarity);

        if (pair.contradiction_score >= threshold) {
            results.push_back(std::move(pair));
        }
    }
    sqlite3_finalize(stmt);

    // Sort by contradiction score descending
    std::sort(results.begin(), results.end(),
        [](const ContradictionPair& a, const ContradictionPair& b) {
            return a.contradiction_score > b.contradiction_score;
        });

    return results;
}

// ── Private helpers ──────────────────────────────────────────────────────

std::vector<ScoredFact> FactRetriever::fts_candidates(const std::string& fts_query, int limit) {
    std::vector<ScoredFact> candidates;
    if (fts_query.empty() || !db_) return candidates;

    const char* sql = R"(
        SELECT f.fact_id, f.content, f.category, f.tags,
               f.trust_score, f.retrieval_count, f.helpful_count,
               f.created_at, f.updated_at,
               -bm25(facts_fts) as fts_rank
        FROM facts f
        JOIN facts_fts ON f.fact_id = facts_fts.rowid
        WHERE facts_fts MATCH ?
        ORDER BY fts_rank DESC
        LIMIT ?
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        EA_WARN("FTS5 prepare failed: {}", sqlite3_errmsg(db_));
        return candidates;
    }

    sqlite3_bind_text(stmt, 1, fts_query.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    double max_rank = 0.0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ScoredFact sf;
        sf.fact.fact_id = sqlite3_column_int(stmt, 0);
        sf.fact.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        sf.fact.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            sf.fact.tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        sf.fact.trust_score = sqlite3_column_double(stmt, 4);
        sf.fact.retrieval_count = sqlite3_column_int(stmt, 5);
        sf.fact.helpful_count = sqlite3_column_int(stmt, 6);
        sf.fact.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
            sf.fact.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        sf.fts_score = sqlite3_column_double(stmt, 9);
        if (sf.fts_score > max_rank) max_rank = sf.fts_score;
        candidates.push_back(std::move(sf));
    }
    sqlite3_finalize(stmt);

    // Normalize FTS scores to [0, 1]
    if (max_rank > 0.0) {
        for (auto& sf : candidates) {
            sf.fts_score = std::max(0.0, sf.fts_score / max_rank);
        }
    }

    return candidates;
}

void FactRetriever::compute_jaccard(std::vector<ScoredFact>& candidates,
                                      const std::vector<std::string>& query_tokens) {
    for (auto& sf : candidates) {
        auto fact_tokens = FtsSanitizer::tokenize(sf.fact.content);
        sf.jaccard_score = FtsSanitizer::jaccard_similarity(query_tokens, fact_tokens);
    }
}

void FactRetriever::apply_trust_and_sort(std::vector<ScoredFact>& candidates,
                                           double min_trust) {
    for (auto& sf : candidates) {
        sf.trust_weight = sf.fact.trust_score;
        sf.final_score = (weights_.fts * sf.fts_score)
                       + (weights_.jaccard * sf.jaccard_score)
                       + (weights_.hrr * sf.hrr_score);
        sf.final_score *= sf.trust_weight;
    }

    // Filter by min_trust
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
            [min_trust](const ScoredFact& sf) { return sf.fact.trust_score < min_trust; }),
        candidates.end());

    // Sort by final score descending
    std::sort(candidates.begin(), candidates.end(),
        [](const ScoredFact& a, const ScoredFact& b) {
            return a.final_score > b.final_score;
        });
}

// ── HRR-enabled methods ──────────────────────────────────────────────────

void FactRetriever::compute_hrr_scores(std::vector<ScoredFact>& candidates,
                                         const std::string& query,
                                         int hrr_dim) {
#ifdef EA_ENABLE_HRR
    if (candidates.empty() || query.empty()) return;

    // Encode the query as an HRR vector
    auto query_vec = hrr::encode_text(query, hrr_dim);

    for (auto& sf : candidates) {
        // Load the fact's HRR vector from the database
        auto fact_phases = load_hrr_vector(sf.fact.fact_id, hrr_dim);
        if (fact_phases.empty()) {
            sf.hrr_score = 0.0;
            continue;
        }

        hrr::HrrVector fact_vec(hrr_dim, std::move(fact_phases));
        sf.hrr_score = hrr::similarity(query_vec, fact_vec);
    }
#else
    (void)candidates;
    (void)query;
    (void)hrr_dim;
#endif
}

Result<std::vector<FactEntry>> FactRetriever::probe_hrr(
    const std::string& entity,
    const std::string& category,
    int limit,
    int hrr_dim) {
#ifdef EA_ENABLE_HRR
    if (!db_) return Error::db("database not open");

    // HRR probe: unbind the entity from the memory bank to find
    // structurally associated facts.
    // 1. Encode the entity
    auto entity_vec = hrr::encode_atom(entity, hrr_dim);

    // 2. Get all facts with HRR vectors stored
    std::string sql = "SELECT f.fact_id, f.content, f.category, f.tags, "
                      "f.trust_score, f.retrieval_count, f.helpful_count, "
                      "f.created_at, f.updated_at, f.hrr_vector "
                      "FROM facts f WHERE f.hrr_vector IS NOT NULL "
                      "AND f.trust_score >= 0.3";
    if (!category.empty()) {
        sql += " AND f.category = ?";
    }
    sql += " LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    int bind_idx = 1;
    if (!category.empty()) {
        sqlite3_bind_text(stmt, bind_idx++, category.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, bind_idx, limit * 3);  // Over-fetch for scoring

    // 3. Score each fact by HRR similarity after unbinding the entity
    std::vector<ScoredFact> scored;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ScoredFact sf;
        sf.fact.fact_id = sqlite3_column_int(stmt, 0);
        sf.fact.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        sf.fact.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            sf.fact.tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        sf.fact.trust_score = sqlite3_column_double(stmt, 4);

        // Deserialize HRR vector
        if (sqlite3_column_type(stmt, 9) != SQLITE_NULL) {
            const auto* blob = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 9));
            int blob_len = sqlite3_column_bytes(stmt, 9);
            auto fact_phases = hrr::bytes_to_phases(blob, blob_len, hrr_dim);
            hrr::HrrVector fact_vec(hrr_dim, std::move(fact_phases));

            // Unbind: probe the memory for the entity
            auto unbound = hrr::unbind(fact_vec, entity_vec);
            // The unbound result should be similar to the content role vector
            // if the fact is about this entity. Use similarity to a reference.
            sf.hrr_score = hrr::similarity(unbound, hrr::encode_text(sf.fact.content, hrr_dim));
        }

        scored.push_back(std::move(sf));
    }
    sqlite3_finalize(stmt);

    // Sort by HRR score
    std::sort(scored.begin(), scored.end(),
        [](const ScoredFact& a, const ScoredFact& b) {
            return a.hrr_score > b.hrr_score;
        });

    // Return top-N
    std::vector<FactEntry> results;
    int n = std::min(limit, static_cast<int>(scored.size()));
    for (int i = 0; i < n; ++i) {
        results.push_back(std::move(scored[i].fact));
    }
    return results;
#else
    (void)entity; (void)category; (void)limit; (void)hrr_dim;
    return std::vector<FactEntry>{};
#endif
}

std::vector<double> FactRetriever::load_hrr_vector(int fact_id, int dim) {
#ifdef EA_ENABLE_HRR
    if (!db_) return {};

    const char* sql = "SELECT hrr_vector FROM facts WHERE fact_id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }

    sqlite3_bind_int(stmt, 1, fact_id);

    std::vector<double> phases;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            const auto* blob = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0));
            int blob_len = sqlite3_column_bytes(stmt, 0);
            phases = hrr::bytes_to_phases(blob, blob_len, dim);
        }
    }
    sqlite3_finalize(stmt);
    return phases;
#else
    (void)fact_id; (void)dim;
    return {};
#endif
}

}  // namespace ea::memory
