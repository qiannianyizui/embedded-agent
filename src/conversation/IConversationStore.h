// IConversationStore — interface for conversation persistence
#pragma once
#include "base/Types.h"
#include "base/Result.h"
#include <string>
#include <vector>

namespace ea::conversation {

struct ConversationMeta {
    std::string id;           // "conv_" + 8 hex chars
    std::string title;        // First user message, truncated to 50 chars
    std::string created_at;   // ISO 8601
    std::string updated_at;   // ISO 8601
    int message_count = 0;
    std::string model;        // Model used (optional)
};

class IConversationStore {
public:
    virtual ~IConversationStore() = default;

    // Create a new conversation, return its ID
    virtual Result<std::string> create(const std::string& model = "") = 0;

    // Append a message to a conversation
    virtual Result<void> append(const std::string& conversation_id,
                                const Message& msg) = 0;

    // Load all messages for a conversation
    virtual Result<std::vector<Message>> load(const std::string& conversation_id) = 0;

    // Load every message including archived (pre-compaction) rows.
    virtual Result<std::vector<Message>> load_all(const std::string& conversation_id) = 0;

    // Mark the current active transcript as archived and store a compressed
    // transcript as the new active context (same conversation id).
    virtual Result<void> archive_and_compact(
        const std::string& conversation_id,
        const std::vector<Message>& compressed_messages) = 0;

    // List conversations (ordered by updated_at descending)
    virtual Result<std::vector<ConversationMeta>> list(int limit = 50, int offset = 0) = 0;

    // Get metadata for a single conversation
    virtual Result<ConversationMeta> get_meta(const std::string& conversation_id) = 0;

    // Delete a conversation
    virtual Result<bool> remove(const std::string& conversation_id) = 0;

    // Export conversation as JSONL string
    virtual Result<std::string> export_jsonl(const std::string& conversation_id) = 0;

    // Import conversation from JSONL string, return new conversation ID
    virtual Result<std::string> import_jsonl(const std::string& jsonl_data,
                                              const std::string& model = "") = 0;

    // Lifecycle
    virtual Result<void> open() = 0;
    virtual Result<void> close() = 0;
};

}  // namespace ea::conversation
