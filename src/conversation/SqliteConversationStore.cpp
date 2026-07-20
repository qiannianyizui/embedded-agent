// SqliteConversationStore — temporary stub (implementation in Task 2)
#include "SqliteConversationStore.h"
#include "common/base/Error.h"

namespace ea::conversation {

SqliteConversationStore::SqliteConversationStore(Config config)
    : config_(std::move(config)) {}

SqliteConversationStore::~SqliteConversationStore() {
    if (opened_) close();
}

Result<std::string> SqliteConversationStore::create(const std::string& /*model*/) {
    return Error::db("not implemented");
}

Result<void> SqliteConversationStore::append(const std::string& /*conversation_id*/,
                                             const Message& /*msg*/) {
    return Error::db("not implemented");
}

Result<std::vector<Message>> SqliteConversationStore::load(const std::string& /*conversation_id*/) {
    return Error::db("not implemented");
}

Result<std::vector<ConversationMeta>> SqliteConversationStore::list(int /*limit*/, int /*offset*/) {
    return Error::db("not implemented");
}

Result<ConversationMeta> SqliteConversationStore::get_meta(const std::string& /*conversation_id*/) {
    return Error::db("not implemented");
}

Result<bool> SqliteConversationStore::remove(const std::string& /*conversation_id*/) {
    return Error::db("not implemented");
}

Result<std::string> SqliteConversationStore::export_jsonl(const std::string& /*conversation_id*/) {
    return Error::db("not implemented");
}

Result<std::string> SqliteConversationStore::import_jsonl(const std::string& /*jsonl_data*/,
                                                          const std::string& /*model*/) {
    return Error::db("not implemented");
}

Result<void> SqliteConversationStore::open() {
    return Error::db("not implemented");
}

Result<void> SqliteConversationStore::close() {
    opened_ = false;
    return {};
}

Result<void> SqliteConversationStore::create_tables() {
    return Error::db("not implemented");
}

Result<void> SqliteConversationStore::update_meta_on_append(
    const std::string& /*conversation_id*/,
    const std::string& /*first_user_content*/) {
    return Error::db("not implemented");
}

std::string SqliteConversationStore::role_to_string(Role /*role*/) const {
    return "";
}

Role SqliteConversationStore::string_to_role(const std::string& /*s*/) const {
    return Role::User;
}

json SqliteConversationStore::message_to_extra_json(const Message& /*msg*/) const {
    return json::object();
}

Message SqliteConversationStore::row_to_message(const std::string& /*role*/,
                                                const std::string& /*content*/,
                                                const std::string& /*extra_json*/) const {
    return Message{Role::User, "", std::nullopt, std::nullopt, std::nullopt};
}

}  // namespace ea::conversation
