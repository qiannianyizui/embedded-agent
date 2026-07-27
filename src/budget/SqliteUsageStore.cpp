// src/budget/SqliteUsageStore.cpp
#include "SqliteUsageStore.h"
#include "log/Logger.h"
#include "io/FileSystem.h"
#include <sqlite3.h>
#include <sstream>
#include <iomanip>
#include <random>
#include <mutex>

namespace ea::budget {

// --- Static members ---
std::mutex SqliteUsageStore::id_mutex_;
std::mt19937 SqliteUsageStore::rng_{std::random_device{}()};

// --- Helpers ---

std::string SqliteUsageStore::generate_id() {
    std::lock_guard<std::mutex> lock(id_mutex_);
    std::stringstream ss;
    ss << "usage_";
    for (int i = 0; i < 4; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (rng_() & 0xFF);
    }
    return ss.str();
}

std::string SqliteUsageStore::current_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    struct tm tm_buf;
    gmtime_r(&time_t, &tm_buf);
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
}

// --- Construction / Lifecycle ---

SqliteUsageStore::SqliteUsageStore(Config config)
    : config_(std::move(config)) {}

SqliteUsageStore::~SqliteUsageStore() {
    if (opened_) close();
}

Result<void> SqliteUsageStore::open() {
    if (opened_) return {};

    EA_DEBUG("SqliteUsageStore::open() path={}", config_.path);

    // Ensure parent directory exists
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

Result<void> SqliteUsageStore::close() {
    if (!opened_) return {};
    sqlite3_close(db_);
    db_ = nullptr;
    opened_ = false;
    return {};
}

Result<void> SqliteUsageStore::create_tables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS usage_records (
            id          TEXT PRIMARY KEY,
            session_id  TEXT NOT NULL DEFAULT '',
            model       TEXT NOT NULL DEFAULT '',
            input_tokens   INTEGER NOT NULL DEFAULT 0,
            output_tokens  INTEGER NOT NULL DEFAULT 0,
            cache_read_tokens  INTEGER DEFAULT 0,
            cache_write_tokens INTEGER DEFAULT 0,
            cost_usd    REAL NOT NULL DEFAULT 0.0,
            timestamp   TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_usage_session ON usage_records(session_id);
        CREATE INDEX IF NOT EXISTS idx_usage_timestamp ON usage_records(timestamp DESC);
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

// --- CRUD ---

Result<std::string> SqliteUsageStore::record(const UsageRecord& rec) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::string id = generate_id();
    std::string ts = rec.timestamp.empty() ? current_iso8601() : rec.timestamp;

    const char* sql = R"(
        INSERT INTO usage_records
            (id, session_id, model, input_tokens, output_tokens,
             cache_read_tokens, cache_write_tokens, cost_usd, timestamp)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, rec.session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, rec.model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, rec.input_tokens);
    sqlite3_bind_int(stmt, 5, rec.output_tokens);
    sqlite3_bind_int(stmt, 6, rec.cache_read_tokens);
    sqlite3_bind_int(stmt, 7, rec.cache_write_tokens);
    sqlite3_bind_double(stmt, 8, rec.cost_usd);
    sqlite3_bind_text(stmt, 9, ts.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("insert failed: ") + sqlite3_errmsg(db_));
    }

    return id;
}

Result<std::vector<UsageRecord>> SqliteUsageStore::query(
        const std::string& session_id, int limit, int offset) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::string sql;
    if (session_id.empty()) {
        sql = "SELECT id, session_id, model, input_tokens, output_tokens, "
              "cache_read_tokens, cache_write_tokens, cost_usd, timestamp "
              "FROM usage_records ORDER BY timestamp DESC LIMIT ? OFFSET ?";
    } else {
        sql = "SELECT id, session_id, model, input_tokens, output_tokens, "
              "cache_read_tokens, cache_write_tokens, cost_usd, timestamp "
              "FROM usage_records WHERE session_id = ? "
              "ORDER BY timestamp DESC LIMIT ? OFFSET ?";
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    int bind_idx = 1;
    if (!session_id.empty()) {
        sqlite3_bind_text(stmt, bind_idx++, session_id.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, bind_idx++, limit);
    sqlite3_bind_int(stmt, bind_idx++, offset);

    std::vector<UsageRecord> results;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        UsageRecord rec;
        rec.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        rec.session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rec.model = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        rec.input_tokens = sqlite3_column_int(stmt, 3);
        rec.output_tokens = sqlite3_column_int(stmt, 4);
        rec.cache_read_tokens = sqlite3_column_int(stmt, 5);
        rec.cache_write_tokens = sqlite3_column_int(stmt, 6);
        rec.cost_usd = sqlite3_column_double(stmt, 7);
        rec.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        results.push_back(std::move(rec));
    }
    sqlite3_finalize(stmt);
    return results;
}

Result<UsageSnapshot> SqliteUsageStore::total_usage(const std::string& session_id) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::string sql;
    if (session_id.empty()) {
        sql = "SELECT COALESCE(SUM(input_tokens), 0), "
              "COALESCE(SUM(output_tokens), 0), "
              "COALESCE(SUM(cache_read_tokens), 0), "
              "COALESCE(SUM(cache_write_tokens), 0) "
              "FROM usage_records";
    } else {
        sql = "SELECT COALESCE(SUM(input_tokens), 0), "
              "COALESCE(SUM(output_tokens), 0), "
              "COALESCE(SUM(cache_read_tokens), 0), "
              "COALESCE(SUM(cache_write_tokens), 0) "
              "FROM usage_records WHERE session_id = ?";
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    if (!session_id.empty()) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    }

    UsageSnapshot snap;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        snap.input_tokens = sqlite3_column_int(stmt, 0);
        snap.output_tokens = sqlite3_column_int(stmt, 1);
        snap.cache_read_tokens = sqlite3_column_int(stmt, 2);
        snap.cache_write_tokens = sqlite3_column_int(stmt, 3);
    }
    sqlite3_finalize(stmt);
    return snap;
}

Result<CostSnapshot> SqliteUsageStore::total_cost(const std::string& session_id) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::string sql;
    if (session_id.empty()) {
        sql = "SELECT COALESCE(SUM(cost_usd), 0.0) FROM usage_records";
    } else {
        sql = "SELECT COALESCE(SUM(cost_usd), 0.0) FROM usage_records WHERE session_id = ?";
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    if (!session_id.empty()) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    }

    CostSnapshot cost;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        // DB stores cost_usd as a single aggregate; put it in input_cost
        // so that CostSnapshot::total() returns the correct value.
        cost.input_cost = sqlite3_column_double(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return cost;
}

Result<bool> SqliteUsageStore::clear(const std::string& session_id) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::string sql;
    if (session_id.empty()) {
        sql = "DELETE FROM usage_records";
    } else {
        sql = "DELETE FROM usage_records WHERE session_id = ?";
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    if (!session_id.empty()) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("delete failed: ") + sqlite3_errmsg(db_));
    }

    return sqlite3_changes(db_) > 0;
}

}  // namespace ea::budget
