#include "HolographicMemory.h"
#include "log/Logger.h"
#include "io/FileSystem.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <regex>
#include <sstream>
#include <iomanip>

#ifdef EA_ENABLE_HRR
#include "hrr/HrrVector.h"
#endif

namespace ea::memory {

static std::string current_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// ── Construction / Destruction ───────────────────────────────────────────

HolographicMemory::HolographicMemory(HolographicMemoryConfig config)
    : config_(std::move(config)) {}

HolographicMemory::~HolographicMemory() {
    if (opened_) close();
}

// ── Lifecycle ────────────────────────────────────────────────────────────

Result<void> HolographicMemory::open() {
    if (opened_) return {};

    EA_DEBUG("HolographicMemory::open() path={}", config_.path);

    // Ensure parent directory exists for file-based databases
    if (config_.path != ":memory:") {
        auto dir = config_.path.substr(0, config_.path.rfind('/'));
        if (!dir.empty()) {
            auto r = fs::mkdir_p(dir);
            if (!r.ok()) return r;
        }
    }

    int rc = sqlite3_open(config_.path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        return Error::db("sqlite3_open failed: " + msg);
    }

    // WAL mode for concurrent access
    if (config_.enable_wal && config_.path != ":memory:") {
        sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    }

    opened_ = true;
    auto table_result = create_tables();
    if (!table_result.ok()) return table_result;

    // Initialize retriever with appropriate weights
    RetrievalWeights weights;
#ifdef EA_ENABLE_HRR
    weights = {0.4, 0.3, 0.3};  // FTS5 + Jaccard + HRR
#endif
    retriever_ = std::make_unique<FactRetriever>(db_, weights);

    return {};
}

Result<void> HolographicMemory::close() {
    if (!opened_) return {};
    retriever_.reset();
    if (db_) {
        int rc = sqlite3_close(db_);
        if (rc == SQLITE_BUSY) {
            return Error::db("sqlite3_close failed: database is busy");
        }
        db_ = nullptr;
    }
    opened_ = false;
    return {};
}

Result<void> HolographicMemory::create_tables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS facts (
            fact_id         INTEGER PRIMARY KEY AUTOINCREMENT,
            content         TEXT NOT NULL UNIQUE,
            category        TEXT NOT NULL DEFAULT 'general',
            tags            TEXT NOT NULL DEFAULT '',
            trust_score     REAL NOT NULL DEFAULT 0.5,
            retrieval_count INTEGER NOT NULL DEFAULT 0,
            helpful_count   INTEGER NOT NULL DEFAULT 0,
            created_at      TEXT NOT NULL,
            updated_at      TEXT NOT NULL,
            hrr_vector      BLOB
        );

        CREATE TABLE IF NOT EXISTS entities (
            entity_id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name        TEXT NOT NULL UNIQUE,
            entity_type TEXT NOT NULL DEFAULT 'auto',
            aliases     TEXT NOT NULL DEFAULT '',
            created_at  TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS fact_entities (
            fact_id   INTEGER NOT NULL,
            entity_id INTEGER NOT NULL,
            PRIMARY KEY (fact_id, entity_id),
            FOREIGN KEY (fact_id) REFERENCES facts(fact_id) ON DELETE CASCADE,
            FOREIGN KEY (entity_id) REFERENCES entities(entity_id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS memory_banks (
            bank_id    INTEGER PRIMARY KEY AUTOINCREMENT,
            bank_name  TEXT NOT NULL UNIQUE,
            vector     BLOB,
            dim        INTEGER NOT NULL DEFAULT 1024,
            fact_count INTEGER NOT NULL DEFAULT 0,
            updated_at TEXT NOT NULL
        );

        CREATE VIRTUAL TABLE IF NOT EXISTS facts_fts
            USING fts5(content, tags, category, content=facts, content_rowid=fact_id);

        -- FTS5 sync triggers
        CREATE TRIGGER IF NOT EXISTS facts_ai AFTER INSERT ON facts BEGIN
            INSERT INTO facts_fts(rowid, content, tags, category)
            VALUES (new.fact_id, new.content, new.tags, new.category);
        END;

        CREATE TRIGGER IF NOT EXISTS facts_ad AFTER DELETE ON facts BEGIN
            INSERT INTO facts_fts(facts_fts, rowid, content, tags, category)
            VALUES ('delete', old.fact_id, old.content, old.tags, old.category);
        END;

        CREATE TRIGGER IF NOT EXISTS facts_au AFTER UPDATE ON facts BEGIN
            INSERT INTO facts_fts(facts_fts, rowid, content, tags, category)
            VALUES ('delete', old.fact_id, old.content, old.tags, old.category);
            INSERT INTO facts_fts(rowid, content, tags, category)
            VALUES (new.fact_id, new.content, new.tags, new.category);
        END;

        -- Indexes for common queries
        CREATE INDEX IF NOT EXISTS idx_facts_category ON facts(category);
        CREATE INDEX IF NOT EXISTS idx_facts_trust ON facts(trust_score);
        CREATE INDEX IF NOT EXISTS idx_entities_name ON entities(name);
        CREATE INDEX IF NOT EXISTS idx_fact_entities_entity ON fact_entities(entity_id);
    )";

    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        return Error::db("create tables failed: " + msg);
    }

    return {};
}

// ── Legacy IMemory CRUD ──────────────────────────────────────────────────

Result<std::string> HolographicMemory::store(const std::string& content,
                                               const std::string& category,
                                               int importance) {
    // Map store → add_fact, using importance to set initial trust
    double initial_trust = 0.3 + (importance / 10.0) * 0.5;  // importance 5 → 0.55, 10 → 0.8
    initial_trust = std::min(initial_trust, config_.trust_ceiling);

    auto r = open();
    if (!r.ok()) return r.error();

    // Extract entities
    auto entities = entity_extractor_.extract(content);

    // Check for duplicate content
    {
        sqlite3_stmt* check = nullptr;
        const char* check_sql = "SELECT fact_id FROM facts WHERE content = ?";
        if (sqlite3_prepare_v2(db_, check_sql, -1, &check, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(check, 1, content.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(check) == SQLITE_ROW) {
                int existing_id = sqlite3_column_int(check, 0);
                sqlite3_finalize(check);
                return std::to_string(existing_id);  // Return existing ID
            }
            sqlite3_finalize(check);
        }
    }

    // Insert fact
    std::string now = current_iso8601();
    const char* sql = "INSERT INTO facts (content, category, trust_score, created_at, updated_at) VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, initial_trust);
    sqlite3_bind_text(stmt, 4, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, now.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("insert failed: ") + sqlite3_errmsg(db_));
    }

    int fact_id = static_cast<int>(sqlite3_last_insert_rowid(db_));

    // Link entities
    for (const auto& entity : entities) {
        auto er = ensure_entity(entity);
        if (er.ok()) {
            link_fact_entity(fact_id, entity);
        }
    }

    // Compute and store HRR vector
#ifdef EA_ENABLE_HRR
    {
        auto hrr_vec = hrr::encode_fact(content, entities, config_.hrr_dim);
        auto hrr_bytes = hrr::phases_to_bytes(hrr_vec);

        const char* upd_sql = "UPDATE facts SET hrr_vector = ? WHERE fact_id = ?";
        sqlite3_stmt* upd_stmt = nullptr;
        if (sqlite3_prepare_v2(db_, upd_sql, -1, &upd_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_blob(upd_stmt, 1, hrr_bytes.data(),
                              static_cast<int>(hrr_bytes.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int(upd_stmt, 2, fact_id);
            sqlite3_step(upd_stmt);
            sqlite3_finalize(upd_stmt);
        }
    }
#endif

    return std::to_string(fact_id);
}

Result<std::vector<MemoryEntry>> HolographicMemory::recall(const std::string& query,
                                                             int limit) {
    // Map recall → search_facts, then convert FactEntry → MemoryEntry
    auto facts_result = search_facts(query, "", 0.0, limit);
    if (!facts_result.ok()) return facts_result.error();

    std::vector<MemoryEntry> entries;
    for (const auto& fe : facts_result.value()) {
        MemoryEntry me;
        me.id = std::to_string(fe.fact_id);
        me.content = fe.content;
        me.category = fe.category;
        me.importance = static_cast<int>(fe.trust_score * 10);
        me.created_at = fe.created_at;
        entries.push_back(std::move(me));
    }
    return entries;
}

Result<bool> HolographicMemory::forget(const std::string& id) {
    // Map forget → remove_fact (id is string, convert to int)
    try {
        int fact_id = std::stoi(id);
        return remove_fact(fact_id);
    } catch (...) {
        return false;
    }
}

Result<std::vector<MemoryEntry>> HolographicMemory::list(int limit, int offset) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::vector<MemoryEntry> entries;
    const char* sql = "SELECT fact_id, content, category, trust_score, created_at "
                      "FROM facts ORDER BY created_at DESC LIMIT ? OFFSET ?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryEntry me;
        me.id = std::to_string(sqlite3_column_int(stmt, 0));
        me.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        me.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        me.importance = static_cast<int>(sqlite3_column_double(stmt, 3) * 10);
        me.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        entries.push_back(std::move(me));
    }
    sqlite3_finalize(stmt);
    return entries;
}

Result<int> HolographicMemory::count() {
    auto r = open();
    if (!r.ok()) return r.error();

    const char* sql = "SELECT COUNT(*) FROM facts";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

// ── System prompt & hooks ────────────────────────────────────────────────

std::string HolographicMemory::system_prompt_block() const {
    if (!db_) return {};

    sqlite3_stmt* stmt;
    const char* sql = "SELECT category, content, trust_score FROM facts "
                       "WHERE trust_score >= 0.6 "
                       "ORDER BY trust_score DESC LIMIT 5";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }

    std::ostringstream ss;
    ss << "# Relevant Memories\n";
    bool has_any = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        has_any = true;
        ss << "- [" << sqlite3_column_text(stmt, 0) << "] "
           << sqlite3_column_text(stmt, 1)
           << " (trust: " << std::fixed << std::setprecision(1)
           << sqlite3_column_double(stmt, 2) << ")\n";
    }
    sqlite3_finalize(stmt);

    return has_any ? ss.str() : std::string{};
}

void HolographicMemory::on_turn_start(const std::string& /*user_input*/) {
    // Could prefetch relevant memories here
}

void HolographicMemory::on_turn_end(const std::string& assistant_output) {
    // Auto-extract facts from assistant output if it contains
    // preference/decision patterns (simple regex-based, no LLM needed)
    if (!opened_ || assistant_output.empty()) return;

    // Pattern: "I prefer X", "I like X", "I use X", "My favorite is X"
    static const std::regex pref_re(
        R"((?:I\s+(?:prefer|like|use|love|recommend|choose)|my\s+favorite\s+(?:is|tool|editor|language)\s+)(.+?)(?:\.|,|$))",
        std::regex::icase);
    for (auto it = std::sregex_iterator(assistant_output.begin(),
                                          assistant_output.end(), pref_re);
         it != std::sregex_iterator(); ++it) {
        std::string fact = "User " + it->str(0);
        // Trim trailing punctuation/whitespace
        while (!fact.empty() && (fact.back() == '.' || fact.back() == ',' ||
               fact.back() == ' ' || fact.back() == '\r' || fact.back() == '\n')) {
            fact.pop_back();
        }
        if (fact.size() > 10) {  // Avoid trivially short facts
            add_fact(fact, "user_pref");
        }
    }
}

void HolographicMemory::on_pre_compress() {
    // Before context compression, could store a summary
}

// ── Holographic extensions ───────────────────────────────────────────────

Result<int> HolographicMemory::add_fact(const std::string& content,
                                          const std::string& category,
                                          const std::string& tags) {
    auto r = open();
    if (!r.ok()) return r.error();

    // Check for duplicate content
    {
        sqlite3_stmt* check = nullptr;
        const char* check_sql = "SELECT fact_id FROM facts WHERE content = ?";
        if (sqlite3_prepare_v2(db_, check_sql, -1, &check, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(check, 1, content.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(check) == SQLITE_ROW) {
                int existing_id = sqlite3_column_int(check, 0);
                sqlite3_finalize(check);
                return existing_id;  // Return existing ID (idempotent)
            }
            sqlite3_finalize(check);
        }
    }

    // Extract entities
    auto entities = entity_extractor_.extract(content);

    // Insert fact
    std::string now = current_iso8601();
    const char* sql = "INSERT INTO facts (content, category, tags, created_at, updated_at) "
                      "VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, tags.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, now.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("insert failed: ") + sqlite3_errmsg(db_));
    }

    int fact_id = static_cast<int>(sqlite3_last_insert_rowid(db_));

    // Link entities
    for (const auto& entity : entities) {
        auto er = ensure_entity(entity);
        if (er.ok()) {
            link_fact_entity(fact_id, entity);
        }
    }

    // Compute and store HRR vector
#ifdef EA_ENABLE_HRR
    {
        auto hrr_vec = hrr::encode_fact(content, entities, config_.hrr_dim);
        auto hrr_bytes = hrr::phases_to_bytes(hrr_vec);

        const char* upd_sql = "UPDATE facts SET hrr_vector = ? WHERE fact_id = ?";
        sqlite3_stmt* upd_stmt = nullptr;
        if (sqlite3_prepare_v2(db_, upd_sql, -1, &upd_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_blob(upd_stmt, 1, hrr_bytes.data(),
                              static_cast<int>(hrr_bytes.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int(upd_stmt, 2, fact_id);
            sqlite3_step(upd_stmt);
            sqlite3_finalize(upd_stmt);
        }
    }
#endif

    return fact_id;
}

Result<std::vector<FactEntry>> HolographicMemory::search_facts(
    const std::string& query, const std::string& category,
    double min_trust, int limit) {
    auto r = open();
    if (!r.ok()) return r.error();

    if (!retriever_) {
        return Error::db("retriever not initialized");
    }

    auto result = retriever_->search(query, category, min_trust, limit);
    if (!result.ok()) return result.error();

    // Populate entities for each fact
    for (auto& fe : result.value()) {
        fe.entities = get_fact_entities(fe.fact_id).value_or(std::vector<std::string>{});
    }

    // Increment retrieval count
    for (const auto& fe : result.value()) {
        sqlite3_stmt* upd = nullptr;
        const char* upd_sql = "UPDATE facts SET retrieval_count = retrieval_count + 1 WHERE fact_id = ?";
        if (sqlite3_prepare_v2(db_, upd_sql, -1, &upd, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(upd, 1, fe.fact_id);
            sqlite3_step(upd);
            sqlite3_finalize(upd);
        }
    }

    return result;
}

Result<bool> HolographicMemory::update_fact(int fact_id,
                                              const std::string* content,
                                              const double* trust_delta,
                                              const std::string* tags,
                                              const std::string* category) {
    auto r = open();
    if (!r.ok()) return r.error();

    // Build dynamic UPDATE statement
    std::string sql = "UPDATE facts SET updated_at = ?";
    int bind_idx = 1;

    // Track which bind positions correspond to which parameters
    int content_bind = -1;
    int trust_floor_bind = -1, trust_delta_bind = -1, trust_ceil_bind = -1;
    int tags_bind = -1;
    int category_bind = -1;

    if (content) {
        sql += ", content = ?";
        content_bind = ++bind_idx;
    }
    if (trust_delta) {
        sql += ", trust_score = MAX(?, MIN(trust_score + ?, ?))";
        trust_floor_bind = ++bind_idx;
        trust_delta_bind = ++bind_idx;
        trust_ceil_bind = ++bind_idx;
    }
    if (tags) {
        sql += ", tags = ?";
        tags_bind = ++bind_idx;
    }
    if (category) {
        sql += ", category = ?";
        category_bind = ++bind_idx;
    }

    sql += " WHERE fact_id = ?";
    int id_bind = ++bind_idx;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    // Bind updated_at
    std::string now = current_iso8601();
    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);

    // Bind optional fields
    if (content_bind > 0) {
        sqlite3_bind_text(stmt, content_bind, content->c_str(), -1, SQLITE_TRANSIENT);
    }
    if (trust_floor_bind > 0) {
        sqlite3_bind_double(stmt, trust_floor_bind, config_.trust_floor);
        sqlite3_bind_double(stmt, trust_delta_bind, *trust_delta);
        sqlite3_bind_double(stmt, trust_ceil_bind, config_.trust_ceiling);
    }
    if (tags_bind > 0) {
        sqlite3_bind_text(stmt, tags_bind, tags->c_str(), -1, SQLITE_TRANSIENT);
    }
    if (category_bind > 0) {
        sqlite3_bind_text(stmt, category_bind, category->c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, id_bind, fact_id);

    int rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("update failed: ") + sqlite3_errmsg(db_));
    }

    // If content changed, re-extract and re-link entities
    if (content && changes > 0) {
        // Remove old entity links
        sqlite3_stmt* del_stmt = nullptr;
        const char* del_sql = "DELETE FROM fact_entities WHERE fact_id = ?";
        if (sqlite3_prepare_v2(db_, del_sql, -1, &del_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(del_stmt, 1, fact_id);
            sqlite3_step(del_stmt);
            sqlite3_finalize(del_stmt);
        }
        // Re-extract and link
        auto entities = entity_extractor_.extract(*content);
        for (const auto& entity : entities) {
            ensure_entity(entity);
            link_fact_entity(fact_id, entity);
        }
    }

    return changes > 0;
}

Result<bool> HolographicMemory::remove_fact(int fact_id) {
    auto r = open();
    if (!r.ok()) return r.error();

    // fact_entities will be cascade-deleted
    const char* sql = "DELETE FROM facts WHERE fact_id = ?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, fact_id);

    int rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("delete failed: ") + sqlite3_errmsg(db_));
    }
    return changes > 0;
}

Result<std::vector<FactEntry>> HolographicMemory::list_facts(
    const std::string& category, double min_trust, int limit) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::string sql = "SELECT fact_id, content, category, tags, trust_score, "
                      "retrieval_count, helpful_count, created_at, updated_at "
                      "FROM facts WHERE trust_score >= ?";

    if (!category.empty()) {
        sql += " AND category = ?";
    }
    sql += " ORDER BY trust_score DESC LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    int bind_idx = 1;
    sqlite3_bind_double(stmt, bind_idx++, min_trust);
    if (!category.empty()) {
        sqlite3_bind_text(stmt, bind_idx++, category.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, bind_idx, limit);

    std::vector<FactEntry> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FactEntry fe = row_to_fact(stmt);
        fe.entities = get_fact_entities(fe.fact_id).value_or(std::vector<std::string>{});
        results.push_back(std::move(fe));
    }
    sqlite3_finalize(stmt);
    return results;
}

// ── Trust feedback ───────────────────────────────────────────────────────

Result<FeedbackResult> HolographicMemory::record_feedback(int fact_id, bool helpful) {
    auto r = open();
    if (!r.ok()) return r.error();

    // Get current trust
    double old_trust = 0.0;
    int helpful_count = 0;
    {
        const char* sel = "SELECT trust_score, helpful_count FROM facts WHERE fact_id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sel, -1, &stmt, nullptr) != SQLITE_OK) {
            return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
        }
        sqlite3_bind_int(stmt, 1, fact_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            old_trust = sqlite3_column_double(stmt, 0);
            helpful_count = sqlite3_column_int(stmt, 1);
        } else {
            sqlite3_finalize(stmt);
            return Error::not_found("fact not found: " + std::to_string(fact_id));
        }
        sqlite3_finalize(stmt);
    }

    // Apply asymmetric feedback
    double delta = helpful ? config_.trust_positive : config_.trust_negative;
    double new_trust = std::max(config_.trust_floor,
                                std::min(config_.trust_ceiling, old_trust + delta));

    // Update
    const char* upd = "UPDATE facts SET trust_score = ?, helpful_count = ?, updated_at = ? WHERE fact_id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, upd, -1, &stmt, nullptr) != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    std::string now = current_iso8601();
    sqlite3_bind_double(stmt, 1, new_trust);
    sqlite3_bind_int(stmt, 2, helpful ? helpful_count + 1 : helpful_count);
    sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, fact_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("update failed: ") + sqlite3_errmsg(db_));
    }

    FeedbackResult result;
    result.fact_id = fact_id;
    result.old_trust = old_trust;
    result.new_trust = new_trust;
    result.helpful_count = helpful ? helpful_count + 1 : helpful_count;
    return result;
}

// ── Algebraic queries ────────────────────────────────────────────────────

Result<std::vector<FactEntry>> HolographicMemory::probe(
    const std::string& entity, const std::string& category, int limit) {
    auto r = open();
    if (!r.ok()) return r.error();
    if (!retriever_) return Error::db("retriever not initialized");

    // Use HRR algebraic probe when available
#ifdef EA_ENABLE_HRR
    auto hrr_result = retriever_->probe_hrr(entity, category, limit, config_.hrr_dim);
    if (hrr_result.ok() && !hrr_result.value().empty()) {
        for (auto& fe : hrr_result.value()) {
            fe.entities = get_fact_entities(fe.fact_id).value_or(std::vector<std::string>{});
        }
        return hrr_result;
    }
#endif

    // Fallback: entity-based lookup
    auto result = retriever_->find_by_entity(entity, category, limit);
    if (!result.ok()) return result.error();

    for (auto& fe : result.value()) {
        fe.entities = get_fact_entities(fe.fact_id).value_or(std::vector<std::string>{});
    }
    return result;
}

Result<std::vector<FactEntry>> HolographicMemory::related(
    const std::string& entity, const std::string& category, int limit) {
    auto r = open();
    if (!r.ok()) return r.error();
    if (!retriever_) return Error::db("retriever not initialized");

    auto result = retriever_->find_related(entity, category, limit);
    if (!result.ok()) return result.error();

    for (auto& fe : result.value()) {
        fe.entities = get_fact_entities(fe.fact_id).value_or(std::vector<std::string>{});
    }
    return result;
}

Result<std::vector<FactEntry>> HolographicMemory::reason(
    const std::vector<std::string>& entities, const std::string& category, int limit) {
    auto r = open();
    if (!r.ok()) return r.error();
    if (!retriever_) return Error::db("retriever not initialized");

    auto result = retriever_->find_by_entities(entities, category, limit);
    if (!result.ok()) return result.error();

    for (auto& fe : result.value()) {
        fe.entities = get_fact_entities(fe.fact_id).value_or(std::vector<std::string>{});
    }
    return result;
}

Result<std::vector<ContradictionPair>> HolographicMemory::contradict(
    const std::string& category, double threshold, int limit) {
    auto r = open();
    if (!r.ok()) return r.error();
    if (!retriever_) return Error::db("retriever not initialized");

    return retriever_->find_contradictions(category, threshold, limit);
}

// ── Private helpers ──────────────────────────────────────────────────────

Result<void> HolographicMemory::ensure_entity(const std::string& name,
                                                const std::string& entity_type) {
    // INSERT OR IGNORE — entity already exists is fine
    const char* sql = "INSERT OR IGNORE INTO entities (name, entity_type, created_at) VALUES (?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    std::string now = current_iso8601();
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, entity_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return {};
}

Result<void> HolographicMemory::link_fact_entity(int fact_id,
                                                    const std::string& entity_name) {
    // Get entity_id
    int entity_id = -1;
    {
        const char* sel = "SELECT entity_id FROM entities WHERE name = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sel, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, entity_name.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                entity_id = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }

    if (entity_id < 0) return {};

    // INSERT OR IGNORE
    const char* sql = "INSERT OR IGNORE INTO fact_entities (fact_id, entity_id) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_int(stmt, 1, fact_id);
    sqlite3_bind_int(stmt, 2, entity_id);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return {};
}

Result<std::vector<std::string>> HolographicMemory::get_fact_entities(int fact_id) const {
    if (!db_) return std::vector<std::string>{};

    const char* sql = "SELECT e.name FROM entities e "
                      "JOIN fact_entities fe ON e.entity_id = fe.entity_id "
                      "WHERE fe.fact_id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::vector<std::string>{};
    }

    sqlite3_bind_int(stmt, 1, fact_id);

    std::vector<std::string> entities;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        entities.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
    return entities;
}

FactEntry HolographicMemory::row_to_fact(void* vstmt) const {
    auto* stmt = static_cast<sqlite3_stmt*>(vstmt);
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
    return fe;
}

// ── Statistics ────────────────────────────────────────────────────────────

Result<HolographicMemory::Stats> HolographicMemory::stats() const {
    auto r = const_cast<HolographicMemory*>(this)->open();
    if (!r.ok()) return r.error();

    Stats s;

    // Total facts
    {
        const char* sql = "SELECT COUNT(*), "
                          "SUM(CASE WHEN trust_score < 0.3 THEN 1 ELSE 0 END), "
                          "SUM(CASE WHEN trust_score >= 0.7 THEN 1 ELSE 0 END) "
                          "FROM facts";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                s.total_facts = sqlite3_column_int(stmt, 0);
                s.low_trust_facts = sqlite3_column_int(stmt, 1);
                s.high_trust_facts = sqlite3_column_int(stmt, 2);
            }
            sqlite3_finalize(stmt);
        }
    }

    // Total entities
    {
        const char* sql = "SELECT COUNT(*) FROM entities";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                s.total_entities = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }

    // DB file size
    if (config_.path != ":memory:") {
        try {
            s.db_size_bytes = std::filesystem::file_size(config_.path);
        } catch (...) {
            s.db_size_bytes = 0;
        }
    }

    return s;
}

}  // namespace ea::memory
