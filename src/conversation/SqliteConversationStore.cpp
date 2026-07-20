// src/conversation/SqliteConversationStore.cpp
#include "SqliteConversationStore.h"
#include "common/io/Logger.h"
#include "common/io/FileSystem.h"
#include <sqlite3.h>
#include <sstream>
#include <iomanip>
#include <random>

namespace ea::conversation {

using json = nlohmann::json;

// --- Helpers ---

static std::string generate_conv_id() {
    static std::mt19937 rng{std::random_device{}()};
    std::stringstream ss;
    ss << "conv_";
    for (int i = 0; i < 4; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (rng() & 0xFF);
    }
    return ss.str();
}

static std::string current_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
}

// --- Construction / Lifecycle ---

SqliteConversationStore::SqliteConversationStore(Config config)
    : config_(std::move(config)) {}

SqliteConversationStore::~SqliteConversationStore() {
    if (opened_) close();
}

Result<void> SqliteConversationStore::open() {
    if (opened_) return {};

    EA_DEBUG("SqliteConversationStore::open() path={}", config_.path);

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

    // Foreign keys for CASCADE delete
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

    opened_ = true;
    return create_tables();
}

Result<void> SqliteConversationStore::close() {
    if (!opened_) return {};
    sqlite3_close(db_);
    db_ = nullptr;
    opened_ = false;
    return {};
}

Result<void> SqliteConversationStore::create_tables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS conversations (
            id          TEXT PRIMARY KEY,
            title       TEXT NOT NULL DEFAULT '',
            model       TEXT DEFAULT '',
            created_at  TEXT NOT NULL,
            updated_at  TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS messages (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            conversation_id TEXT NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
            seq             INTEGER NOT NULL,
            role            TEXT NOT NULL,
            content         TEXT NOT NULL DEFAULT '',
            extra_json      TEXT DEFAULT '{}'
        );
        CREATE INDEX IF NOT EXISTS idx_messages_conv_seq ON messages(conversation_id, seq);
        CREATE INDEX IF NOT EXISTS idx_conversations_updated ON conversations(updated_at DESC);
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

// --- Role conversion ---

std::string SqliteConversationStore::role_to_string(Role role) const {
    switch (role) {
        case Role::System:    return "system";
        case Role::User:      return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool:      return "tool";
    }
    return "unknown";
}

Role SqliteConversationStore::string_to_role(const std::string& s) const {
    if (s == "system")    return Role::System;
    if (s == "user")      return Role::User;
    if (s == "assistant") return Role::Assistant;
    if (s == "tool")      return Role::Tool;
    return Role::System;  // fallback
}

// --- Message serialization ---

json SqliteConversationStore::message_to_extra_json(const Message& msg) const {
    json extra;
    if (msg.name.has_value()) {
        extra["name"] = msg.name.value();
    }
    if (msg.tool_calls.has_value()) {
        json tc_arr = json::array();
        for (const auto& tc : msg.tool_calls.value()) {
            json tc_obj;
            tc_obj["id"] = tc.id;
            tc_obj["name"] = tc.name;
            tc_obj["arguments"] = tc.arguments;
            tc_arr.push_back(tc_obj);
        }
        extra["tool_calls"] = tc_arr;
    }
    if (msg.tool_call_id.has_value()) {
        extra["tool_call_id"] = msg.tool_call_id.value();
    }
    return extra;
}

Message SqliteConversationStore::row_to_message(const std::string& role,
                                                  const std::string& content,
                                                  const std::string& extra_json_str) const {
    Message msg;
    msg.role = string_to_role(role);
    msg.content = content;

    if (!extra_json_str.empty() && extra_json_str != "{}") {
        try {
            auto extra = json::parse(extra_json_str);
            if (extra.contains("name")) {
                msg.name = extra["name"].get<std::string>();
            }
            if (extra.contains("tool_calls")) {
                std::vector<ToolCall> tcs;
                for (const auto& tc_json : extra["tool_calls"]) {
                    ToolCall tc;
                    tc.id = tc_json["id"].get<std::string>();
                    tc.name = tc_json["name"].get<std::string>();
                    tc.arguments = tc_json["arguments"];
                    tcs.push_back(std::move(tc));
                }
                msg.tool_calls = std::move(tcs);
            }
            if (extra.contains("tool_call_id")) {
                msg.tool_call_id = extra["tool_call_id"].get<std::string>();
            }
        } catch (...) {
            // Malformed extra_json — skip optional fields
        }
    }
    return msg;
}

// --- CRUD ---

Result<std::string> SqliteConversationStore::create(const std::string& model) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::string id = generate_conv_id();
    std::string now = current_iso8601();

    const char* sql = "INSERT INTO conversations (id, title, model, created_at, updated_at) VALUES (?, '', ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, now.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("insert failed: ") + sqlite3_errmsg(db_));
    }

    return id;
}

Result<void> SqliteConversationStore::append(const std::string& conversation_id,
                                               const Message& msg) {
    auto r = open();
    if (!r.ok()) return r.error();

    // Get next seq number
    const char* seq_sql = "SELECT COALESCE(MAX(seq), -1) + 1 FROM messages WHERE conversation_id = ?";
    sqlite3_stmt* seq_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, seq_sql, -1, &seq_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare seq failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(seq_stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);

    int next_seq = 0;
    rc = sqlite3_step(seq_stmt);
    if (rc == SQLITE_ROW) {
        next_seq = sqlite3_column_int(seq_stmt, 0);
    }
    sqlite3_finalize(seq_stmt);

    // Insert message
    std::string extra = message_to_extra_json(msg).dump();
    std::string role_str = role_to_string(msg.role);

    const char* sql = "INSERT INTO messages (conversation_id, seq, role, content, extra_json) VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, next_seq);
    sqlite3_bind_text(stmt, 3, role_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, msg.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, extra.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("insert message failed: ") + sqlite3_errmsg(db_));
    }

    // Update conversation metadata (title, updated_at)
    std::string first_user_content;
    if (msg.role == Role::User) {
        first_user_content = msg.content;
    }
    return update_meta_on_append(conversation_id, first_user_content);
}

Result<void> SqliteConversationStore::update_meta_on_append(
        const std::string& conversation_id,
        const std::string& first_user_content) {
    std::string now = current_iso8601();

    // Build UPDATE statement — optionally set title
    std::string sql;
    if (!first_user_content.empty()) {
        // Check if title is still empty
        sql = "UPDATE conversations SET updated_at = ?, "
              "title = CASE WHEN title = '' THEN ? ELSE title END "
              "WHERE id = ?";
    } else {
        sql = "UPDATE conversations SET updated_at = ? WHERE id = ?";
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare update failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);
    if (!first_user_content.empty()) {
        std::string truncated = first_user_content.substr(0, 50);
        sqlite3_bind_text(stmt, 2, truncated.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, conversation_id.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_text(stmt, 2, conversation_id.c_str(), -1, SQLITE_TRANSIENT);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("update conversation failed: ") + sqlite3_errmsg(db_));
    }
    return {};
}

Result<std::vector<Message>> SqliteConversationStore::load(const std::string& conversation_id) {
    auto r = open();
    if (!r.ok()) return r.error();

    // Verify conversation exists
    const char* check_sql = "SELECT id FROM conversations WHERE id = ?";
    sqlite3_stmt* check_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, check_sql, -1, &check_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare check failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(check_stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(check_stmt);
    sqlite3_finalize(check_stmt);

    if (rc != SQLITE_ROW) {
        return Error::not_found("Conversation not found: " + conversation_id);
    }

    // Load messages
    const char* sql = "SELECT role, content, extra_json FROM messages "
                      "WHERE conversation_id = ? ORDER BY seq ASC";
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<Message> messages;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        std::string role(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        std::string content(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        std::string extra(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        messages.push_back(row_to_message(role, content, extra));
    }
    sqlite3_finalize(stmt);
    return messages;
}

Result<std::vector<ConversationMeta>> SqliteConversationStore::list(int limit, int offset) {
    auto r = open();
    if (!r.ok()) return r.error();

    const char* sql = R"(
        SELECT c.id, c.title, c.model, c.created_at, c.updated_at,
               COUNT(m.id) as msg_count
        FROM conversations c
        LEFT JOIN messages m ON c.id = m.conversation_id
        GROUP BY c.id
        ORDER BY c.updated_at DESC
        LIMIT ? OFFSET ?
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    std::vector<ConversationMeta> result;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ConversationMeta meta;
        meta.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        meta.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        meta.model = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        meta.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        meta.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        meta.message_count = sqlite3_column_int(stmt, 5);
        result.push_back(std::move(meta));
    }
    sqlite3_finalize(stmt);
    return result;
}

Result<ConversationMeta> SqliteConversationStore::get_meta(const std::string& conversation_id) {
    auto r = open();
    if (!r.ok()) return r.error();

    const char* sql = R"(
        SELECT c.id, c.title, c.model, c.created_at, c.updated_at,
               COUNT(m.id) as msg_count
        FROM conversations c
        LEFT JOIN messages m ON c.id = m.conversation_id
        WHERE c.id = ?
        GROUP BY c.id
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return Error::not_found("Conversation not found: " + conversation_id);
    }

    ConversationMeta meta;
    meta.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    meta.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    meta.model = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    meta.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    meta.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    meta.message_count = sqlite3_column_int(stmt, 5);
    sqlite3_finalize(stmt);
    return meta;
}

Result<bool> SqliteConversationStore::remove(const std::string& conversation_id) {
    auto r = open();
    if (!r.ok()) return r.error();

    const char* sql = "DELETE FROM conversations WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("delete failed: ") + sqlite3_errmsg(db_));
    }

    return sqlite3_changes(db_) > 0;
}

// --- JSONL ---

Result<std::string> SqliteConversationStore::export_jsonl(const std::string& conversation_id) {
    auto msgs = load(conversation_id);
    if (!msgs.ok()) return msgs.error();

    std::string result;
    for (const auto& msg : msgs.value()) {
        json line;
        line["role"] = role_to_string(msg.role);
        line["content"] = msg.content;
        if (msg.name.has_value()) {
            line["name"] = msg.name.value();
        }
        if (msg.tool_calls.has_value()) {
            json tc_arr = json::array();
            for (const auto& tc : msg.tool_calls.value()) {
                json tc_obj;
                tc_obj["id"] = tc.id;
                tc_obj["name"] = tc.name;
                tc_obj["arguments"] = tc.arguments;
                tc_arr.push_back(tc_obj);
            }
            line["tool_calls"] = tc_arr;
        }
        if (msg.tool_call_id.has_value()) {
            line["tool_call_id"] = msg.tool_call_id.value();
        }
        result += line.dump() + "\n";
    }
    return result;
}

Result<std::string> SqliteConversationStore::import_jsonl(const std::string& jsonl_data,
                                                            const std::string& model) {
    auto r = open();
    if (!r.ok()) return r.error();

    // Create new conversation
    auto conv_r = create(model);
    if (!conv_r.ok()) return conv_r.error();
    std::string conv_id = conv_r.value();

    // Parse each line
    std::istringstream stream(jsonl_data);
    std::string line;
    int line_num = 0;
    while (std::getline(stream, line)) {
        line_num++;
        if (line.empty()) continue;

        json obj;
        try {
            obj = json::parse(line);
        } catch (...) {
            return Error::parse("JSONL parse error at line " + std::to_string(line_num));
        }

        if (!obj.contains("role") || !obj["role"].is_string()) {
            return Error::parse("JSONL missing 'role' at line " + std::to_string(line_num));
        }

        Message msg;
        msg.role = string_to_role(obj["role"].get<std::string>());
        msg.content = obj.value("content", std::string{});

        if (obj.contains("name") && obj["name"].is_string()) {
            msg.name = obj["name"].get<std::string>();
        }
        if (obj.contains("tool_calls") && obj["tool_calls"].is_array()) {
            std::vector<ToolCall> tcs;
            for (const auto& tc_json : obj["tool_calls"]) {
                ToolCall tc;
                tc.id = tc_json.value("id", std::string{});
                tc.name = tc_json.value("name", std::string{});
                if (tc_json.contains("arguments")) {
                    tc.arguments = tc_json["arguments"];
                }
                tcs.push_back(std::move(tc));
            }
            msg.tool_calls = std::move(tcs);
        }
        if (obj.contains("tool_call_id") && obj["tool_call_id"].is_string()) {
            msg.tool_call_id = obj["tool_call_id"].get<std::string>();
        }

        auto append_r = append(conv_id, msg);
        if (!append_r.ok()) {
            // Best effort: remove partial conversation
            remove(conv_id);
            return append_r.error();
        }
    }

    return conv_id;
}

}  // namespace ea::conversation
