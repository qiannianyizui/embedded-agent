#include "SqliteMemory.h"
#include "common/io/Logger.h"
#include "common/io/FileSystem.h"
#include <sqlite3.h>
#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace ea::memory {

static std::string generate_uuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    const char* hex = "0123456789abcdef";
    std::string uuid;
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) uuid += '-';
        uuid += hex[dis(gen)];
    }
    return uuid;
}

static std::string current_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

SqliteMemory::SqliteMemory(Config config) : config_(std::move(config)) {}

SqliteMemory::~SqliteMemory() {
    if (opened_) close();
}

Result<void> SqliteMemory::open() {
    if (opened_) return {};

    EA_DEBUG("SqliteMemory::open() path={}", config_.path);

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
    return create_tables();
}

Result<void> SqliteMemory::create_tables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS memories (
            id          TEXT PRIMARY KEY,
            content     TEXT NOT NULL,
            category    TEXT NOT NULL DEFAULT 'core',
            importance  INTEGER NOT NULL DEFAULT 5,
            agent_id    TEXT,
            created_at  TEXT NOT NULL,
            accessed_at TEXT NOT NULL
        );
    )";

    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        return Error::db("create table failed: " + msg);
    }

    if (config_.enable_fts5) {
        const char* fts_sql = R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts
                USING fts5(content, category, agent_id,
                           content='memories',
                           content_rowid='rowid');
        )";
        rc = sqlite3_exec(db_, fts_sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "unknown";
            sqlite3_free(err);
            return Error::db("create FTS5 table failed: " + msg);
        }

        // FTS5 sync triggers
        const char* triggers = R"(
            CREATE TRIGGER IF NOT EXISTS memories_ai AFTER INSERT ON memories BEGIN
                INSERT INTO memories_fts(rowid, content, category, agent_id)
                VALUES (new.rowid, new.content, new.category, new.agent_id);
            END;
            CREATE TRIGGER IF NOT EXISTS memories_ad AFTER DELETE ON memories BEGIN
                INSERT INTO memories_fts(memories_fts, rowid, content, category, agent_id)
                VALUES ('delete', old.rowid, old.content, old.category, old.agent_id);
            END;
            CREATE TRIGGER IF NOT EXISTS memories_au AFTER UPDATE ON memories BEGIN
                INSERT INTO memories_fts(memories_fts, rowid, content, category, agent_id)
                VALUES ('delete', old.rowid, old.content, old.category, old.agent_id);
                INSERT INTO memories_fts(rowid, content, category, agent_id)
                VALUES (new.rowid, new.content, new.category, new.agent_id);
            END;
        )";
        rc = sqlite3_exec(db_, triggers, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "unknown";
            sqlite3_free(err);
            return Error::db("create triggers failed: " + msg);
        }
    }

    return {};
}

Result<void> SqliteMemory::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    opened_ = false;
    return {};
}

Result<std::string> SqliteMemory::store(const std::string& content,
                                          const std::string& category,
                                          int importance) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::string id = generate_uuid();
    std::string now = current_iso8601();

    const char* sql = "INSERT INTO memories (id, content, category, importance, created_at, accessed_at) VALUES (?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, importance);
    sqlite3_bind_text(stmt, 5, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, now.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("insert failed: ") + sqlite3_errmsg(db_));
    }

    return id;
}

Result<std::vector<MemoryEntry>> SqliteMemory::recall(const std::string& query,
                                                        int limit) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::vector<MemoryEntry> results;

    if (config_.enable_fts5) {
        const char* sql = R"(
            SELECT m.id, m.content, m.category, m.importance, m.created_at, m.agent_id
            FROM memories m
            JOIN memories_fts fts ON m.rowid = fts.rowid
            WHERE memories_fts MATCH ?
            ORDER BY bm25(memories_fts) DESC
            LIMIT ?
        )";
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, limit);

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                MemoryEntry entry;
                entry.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                entry.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                entry.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                entry.importance = sqlite3_column_int(stmt, 3);
                entry.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
                    entry.agent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                }
                results.push_back(std::move(entry));
            }
            sqlite3_finalize(stmt);
        }
        // Fallback to LIKE if FTS5 returns no results (e.g. special chars in query)
        if (results.empty()) {
            const char* like_sql = "SELECT id, content, category, importance, created_at, agent_id FROM memories WHERE content LIKE ? LIMIT ?";
            sqlite3_stmt* like_stmt = nullptr;
            sqlite3_prepare_v2(db_, like_sql, -1, &like_stmt, nullptr);
            std::string pattern = "%" + query + "%";
            sqlite3_bind_text(like_stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(like_stmt, 2, limit);

            while (sqlite3_step(like_stmt) == SQLITE_ROW) {
                MemoryEntry entry;
                entry.id = reinterpret_cast<const char*>(sqlite3_column_text(like_stmt, 0));
                entry.content = reinterpret_cast<const char*>(sqlite3_column_text(like_stmt, 1));
                entry.category = reinterpret_cast<const char*>(sqlite3_column_text(like_stmt, 2));
                entry.importance = sqlite3_column_int(like_stmt, 3);
                entry.created_at = reinterpret_cast<const char*>(sqlite3_column_text(like_stmt, 4));
                if (sqlite3_column_type(like_stmt, 5) != SQLITE_NULL) {
                    entry.agent_id = reinterpret_cast<const char*>(sqlite3_column_text(like_stmt, 5));
                }
                results.push_back(std::move(entry));
            }
            sqlite3_finalize(like_stmt);
        }
    } else {
        // Fallback: LIKE search
        const char* sql = "SELECT id, content, category, importance, created_at, agent_id FROM memories WHERE content LIKE ? LIMIT ?";
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        std::string pattern = "%" + query + "%";
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            MemoryEntry entry;
            entry.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            entry.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            entry.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            entry.importance = sqlite3_column_int(stmt, 3);
            entry.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
                entry.agent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            }
            results.push_back(std::move(entry));
        }
        sqlite3_finalize(stmt);
    }

    return results;
}

Result<bool> SqliteMemory::forget(const std::string& id) {
    auto r = open();
    if (!r.ok()) return r.error();

    const char* sql = "DELETE FROM memories WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("delete failed: ") + sqlite3_errmsg(db_));
    }
    return sqlite3_changes(db_) > 0;
}

Result<std::vector<MemoryEntry>> SqliteMemory::list(int limit, int offset) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::vector<MemoryEntry> results;
    const char* sql = "SELECT id, content, category, importance, created_at, agent_id FROM memories ORDER BY created_at DESC LIMIT ? OFFSET ?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryEntry entry;
        entry.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        entry.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        entry.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        entry.importance = sqlite3_column_int(stmt, 3);
        entry.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
            entry.agent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        }
        results.push_back(std::move(entry));
    }
    sqlite3_finalize(stmt);
    return results;
}

Result<int> SqliteMemory::count() {
    auto r = open();
    if (!r.ok()) return r.error();

    const char* sql = "SELECT COUNT(*) FROM memories";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

std::string SqliteMemory::system_prompt_block() const {
    if (!db_) return {};

    // Query recent high-importance memories
    sqlite3_stmt* stmt;
    const char* sql = "SELECT category, content FROM memories "
                       "WHERE importance >= 6 "
                       "ORDER BY created_at DESC LIMIT 5";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }

    std::ostringstream ss;
    ss << "# Relevant Memories\n";
    bool has_any = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        has_any = true;
        ss << "- [" << sqlite3_column_text(stmt, 0) << "] "
           << sqlite3_column_text(stmt, 1) << "\n";
    }
    sqlite3_finalize(stmt);

    return has_any ? ss.str() : std::string{};
}

void SqliteMemory::on_turn_end(const std::string& /*assistant_output*/) {
    // Auto-store could be implemented here when auto_memory is enabled
    // For now, this is a no-op placeholder
}

void SqliteMemory::on_pre_compress() {
    // Before context compression, could store a summary
    // For now, this is a no-op placeholder
}

}  // namespace ea::memory
