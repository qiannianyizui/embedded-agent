// SqliteConversationStore — SQLite-backed conversation persistence
#pragma once
#include "IConversationStore.h"
#include <string>
#include <memory>

struct sqlite3;

namespace ea::conversation {

class SqliteConversationStore : public IConversationStore {
public:
    struct Config {
        std::string path;       // Path to conversations.db
        bool enable_wal = true;
    };

    explicit SqliteConversationStore(Config config);
    ~SqliteConversationStore() override;

    SqliteConversationStore(const SqliteConversationStore&) = delete;
    SqliteConversationStore& operator=(const SqliteConversationStore&) = delete;

    Result<std::string> create(const std::string& model = "") override;
    Result<void> append(const std::string& conversation_id, const Message& msg) override;
    Result<std::vector<Message>> load(const std::string& conversation_id) override;
    Result<std::vector<ConversationMeta>> list(int limit = 50, int offset = 0) override;
    Result<ConversationMeta> get_meta(const std::string& conversation_id) override;
    Result<bool> remove(const std::string& conversation_id) override;
    Result<std::string> export_jsonl(const std::string& conversation_id) override;
    Result<std::string> import_jsonl(const std::string& jsonl_data,
                                      const std::string& model = "") override;
    Result<void> open() override;
    Result<void> close() override;

private:
    Result<void> create_tables();
    Result<void> update_meta_on_append(const std::string& conversation_id,
                                        const std::string& first_user_content);
    std::string role_to_string(Role role) const;
    Role string_to_role(const std::string& s) const;
    json message_to_extra_json(const Message& msg) const;
    Message row_to_message(const std::string& role, const std::string& content,
                           const std::string& extra_json) const;

    Config config_;
    sqlite3* db_ = nullptr;
    bool opened_ = false;
};

}  // namespace ea::conversation
