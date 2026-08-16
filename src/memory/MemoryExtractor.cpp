// MemoryExtractor — background-thread batch memory extraction.
#include "MemoryExtractor.h"
#include "log/Logger.h"
#include <sqlite3.h>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <ctime>
#include <chrono>

namespace ea::memory {

namespace {

// ISO-8601 UTC with milliseconds, same format as SqliteConversationStore
// ("%Y-%m-%dT%H:%M:%S.%fZ"). Fixed-width so lexicographic comparison is
// chronological.
std::string iso_for(std::chrono::system_clock::time_point tp) {
    auto tt = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;
    struct tm tm_buf;
    gmtime_r(&tt, &tm_buf);
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S") << '.'
       << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
}

const char* DEFAULT_EXTRACTION_PROMPT =
    "Extract key facts from this conversation segment. Return each fact as a "
    "separate line starting with \"- \".\n"
    "Focus on: user preferences, decisions made, important entities, "
    "constraints, and outcomes.\n"
    "Omit: greetings, acknowledgments, and routine exchanges.\n\n"
    "{user_input}";

}  // anonymous namespace

MemoryExtractor::MemoryExtractor(std::shared_ptr<IProvider> provider,
                                 IMemory* memory,
                                 std::string conversations_db_path,
                                 Config config)
    : provider_(std::move(provider)),
      memory_(memory),
      db_path_(std::move(conversations_db_path)),
      config_(std::move(config)) {}

MemoryExtractor::~MemoryExtractor() {
    shutdown();
}

void MemoryExtractor::start() {
    if (started_.exchange(true)) return;
    if (!config_.enable || !provider_ || !memory_ || db_path_.empty()) {
        return;
    }
    worker_ = std::thread([this] { worker_loop(); });
}

void MemoryExtractor::enqueue(const std::string& conversation_id) {
    if (conversation_id.empty()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return;
        if (queued_.count(conversation_id)) return;  // Dedup
        queued_.insert(conversation_id);
        queue_.push_back(conversation_id);
    }
    cv_.notify_one();
}

void MemoryExtractor::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return;
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------

void MemoryExtractor::worker_loop() {
    open_db();
    if (db_) {
        startup_catch_up();
    }

    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (queue_.empty()) {
            if (stop_) break;
            continue;
        }
        std::string id = std::move(queue_.front());
        queue_.pop_front();
        lock.unlock();
        process(id);
        lock.lock();
    }
}

void MemoryExtractor::startup_catch_up() {
    const char* sql =
        "SELECT c.id FROM conversations c "
        "LEFT JOIN extraction_progress p ON p.conversation_id = c.id "
        "WHERE EXISTS ("
        "  SELECT 1 FROM messages m "
        "  WHERE m.conversation_id = c.id AND m.active = 1 "
        "    AND m.id > COALESCE(p.last_message_id, 0)) "
        "  AND EXISTS ("
        "  SELECT 1 FROM messages m2 "
        "  WHERE m2.conversation_id = c.id AND m2.role = 'user')";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        EA_WARN("MemoryExtractor: catch-up prepare failed: {}", sqlite3_errmsg(db_));
        return;
    }
    std::vector<std::string> pending;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* id = sqlite3_column_text(stmt, 0);
        if (id) pending.emplace_back(reinterpret_cast<const char*>(id));
    }
    sqlite3_finalize(stmt);
    if (!pending.empty()) {
        EA_INFO("MemoryExtractor: catch-up enqueued {} conversation(s)", pending.size());
    }
    for (const auto& cid : pending) {
        enqueue(cid);
    }
}

void MemoryExtractor::process(const std::string& conversation_id) {
    if (!db_ && !open_db()) {
        // Give up on this item; a later trigger will retry.
        std::lock_guard<std::mutex> lock(mutex_);
        queued_.erase(conversation_id);
        return;
    }

    // --- Load incremental messages (capped). ---
    auto watermark = get_watermark(conversation_id);
    if (!watermark.ok()) {
        EA_WARN("MemoryExtractor: watermark read failed: {}", watermark.error().message);
        std::lock_guard<std::mutex> lock(mutex_);
        queued_.erase(conversation_id);
        return;
    }

    // --- Load incremental messages (capped). ---
    const char* load_sql =
        "SELECT id, role, content FROM ("
        "  SELECT id, role, content FROM messages"
        "  WHERE conversation_id = ? AND active = 1 AND id > ?"
        "    AND role IN ('user','assistant')"
        "  ORDER BY id DESC LIMIT ?"
        ") ORDER BY id ASC";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, load_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        EA_WARN("MemoryExtractor: load prepare failed: {}", sqlite3_errmsg(db_));
        std::lock_guard<std::mutex> lock(mutex_);
        queued_.erase(conversation_id);
        return;
    }
    sqlite3_bind_text(stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, watermark.value());
    sqlite3_bind_int(stmt, 3, config_.max_messages);

    std::ostringstream batch;
    bool has_user = false;
    long long last_id = watermark.value();
    int budget = config_.max_chars;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        long long msg_id = sqlite3_column_int64(stmt, 0);
        const unsigned char* role = sqlite3_column_text(stmt, 1);
        const unsigned char* content = sqlite3_column_text(stmt, 2);
        last_id = msg_id;
        if (role && std::strcmp(reinterpret_cast<const char*>(role), "user") == 0) {
            has_user = true;
        }
        if (!content) continue;
        std::string text = reinterpret_cast<const char*>(content);
        if (static_cast<int>(text.size()) > budget) {
            text.resize(budget);
        }
        if (text.empty()) continue;
        batch << (role ? reinterpret_cast<const char*>(role) : "?") << ": "
              << text << "\n\n";
        budget -= static_cast<int>(text.size()) + 8;
        if (budget <= 0) break;
    }
    sqlite3_finalize(stmt);

    if (!has_user) {
        // Nothing extractable (e.g. system-only or zero content) — leave the
        // watermark untouched so a later trigger can still pick up real turns.
        std::lock_guard<std::mutex> lock(mutex_);
        queued_.erase(conversation_id);
        return;
    }

    // --- LLM extraction. ---
    std::string prompt = config_.extraction_prompt.empty()
        ? DEFAULT_EXTRACTION_PROMPT
        : config_.extraction_prompt;
    size_t pos;
    while ((pos = prompt.find("{user_input}")) != std::string::npos) {
        prompt.replace(pos, 12, batch.str());
    }
    while ((pos = prompt.find("{assistant_output}")) != std::string::npos) {
        prompt.replace(pos, 18, "");
    }

    std::vector<Message> req = {
        {Role::System, "You are a fact extraction assistant. Extract only factual information.",
         std::nullopt, std::nullopt, std::nullopt},
        {Role::User, prompt, std::nullopt, std::nullopt, std::nullopt}
    };
    ChatOptions opts;
    auto response = provider_->chat(req, {}, "", opts);
    if (!response.ok()) {
        // Watermark not advanced → next trigger retries this batch.
        EA_WARN("MemoryExtractor: extraction failed for {}: {}", conversation_id,
                response.error().message);
        std::lock_guard<std::mutex> lock(mutex_);
        queued_.erase(conversation_id);
        return;
    }

    // --- Parse "- " lines and store as long-term facts. ---
    std::istringstream stream(response.value().content);
    std::string line;
    int count = 0;
    while (std::getline(stream, line)) {
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.erase(line.begin());
        }
        while (!line.empty() &&
               (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
            line.pop_back();
        }
        if (line.size() <= 2 || line[0] != '-' || line[1] != ' ') continue;
        std::string fact = line.substr(2);
        if (fact.empty()) continue;

        if (memory_->is_holographic()) {
            auto r = memory_->add_fact(fact, "long_term");
            if (!r.ok()) {
                EA_WARN("MemoryExtractor: add_fact failed: {}", r.error().message);
            } else {
                ++count;
            }
        } else {
            auto r = memory_->store(fact, "long_term", config_.long_term_importance);
            if (!r.ok()) {
                EA_WARN("MemoryExtractor: store failed: {}", r.error().message);
            } else {
                ++count;
            }
        }
    }

    // --- Advance watermark only after a successful extraction. ---
    auto wm = set_watermark(conversation_id, last_id);
    if (!wm.ok()) {
        EA_WARN("MemoryExtractor: watermark write failed: {}", wm.error().message);
    }
    EA_DEBUG("MemoryExtractor: extracted {} fact(s) from {} (watermark {})",
             count, conversation_id, last_id);

    std::lock_guard<std::mutex> lock(mutex_);
    queued_.erase(conversation_id);
}

// ---------------------------------------------------------------------------
// DB helpers (worker thread only)
// ---------------------------------------------------------------------------

bool MemoryExtractor::open_db() {
    if (db_) return true;
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        EA_WARN("MemoryExtractor: open {} failed: {}", db_path_, sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    sqlite3_busy_timeout(db_, 5000);
    // Defensive: the store creates this table, but tolerate dbs opened by
    // older builds or read-side-only setups.
    sqlite3_exec(db_,
        "CREATE TABLE IF NOT EXISTS extraction_progress ("
        "conversation_id TEXT PRIMARY KEY,"
        "last_message_id INTEGER NOT NULL,"
        "updated_at TEXT NOT NULL);",
        nullptr, nullptr, nullptr);
    return true;
}

void MemoryExtractor::close_db() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

Result<long long> MemoryExtractor::get_watermark(const std::string& conversation_id) {
    const char* sql = "SELECT last_message_id FROM extraction_progress WHERE conversation_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare watermark: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);
    long long wm = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        wm = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return wm;
}

Result<void> MemoryExtractor::set_watermark(const std::string& conversation_id,
                                            long long last_message_id) {
    std::string now = iso_for(std::chrono::system_clock::now());
    const char* upd = "UPDATE extraction_progress SET last_message_id = ?, updated_at = ?"
                      " WHERE conversation_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, upd, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare wm update: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_int64(stmt, 1, last_message_id);
    sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, conversation_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (sqlite3_changes(db_) == 0) {
        const char* ins =
            "INSERT INTO extraction_progress (conversation_id, last_message_id, updated_at)"
            " VALUES (?, ?, ?)";
        sqlite3_stmt* istmt = nullptr;
        rc = sqlite3_prepare_v2(db_, ins, -1, &istmt, nullptr);
        if (rc != SQLITE_OK) {
            return Error::db(std::string("prepare wm insert: ") + sqlite3_errmsg(db_));
        }
        sqlite3_bind_text(istmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(istmt, 2, last_message_id);
        sqlite3_bind_text(istmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(istmt);
        sqlite3_finalize(istmt);
        if (rc != SQLITE_DONE) {
            return Error::db(std::string("watermark insert: ") + sqlite3_errmsg(db_));
        }
    }
    return {};
}

}  // namespace ea::memory
