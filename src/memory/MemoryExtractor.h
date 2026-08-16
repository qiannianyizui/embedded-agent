// MemoryExtractor — background-thread batch memory extraction.
// Reads conversation transcripts from conversations.db (never deleted),
// extracts durable facts via LLM, and stores them in long-term memory.
// Triggered at session boundaries (new/resume/quit) and on startup catch-up.
#pragma once
#include "base/Result.h"
#include "provider/IProvider.h"
#include "memory/IMemory.h"
#include <string>
#include <deque>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>

struct sqlite3;

namespace ea::memory {

class MemoryExtractor {
public:
    struct Config {
        bool enable = true;               // Master switch (enable_fact_extraction)
        int long_term_importance = 8;     // Importance for extracted facts
        int max_messages = 50;            // Max user/assistant messages per batch
        int max_chars = 16000;            // Max serialized chars per batch
        std::string extraction_prompt;    // Custom prompt (empty = default)
    };

    // provider/memory must outlive this object (join on shutdown).
    MemoryExtractor(std::shared_ptr<IProvider> provider, IMemory* memory,
                    std::string conversations_db_path, Config config);
    ~MemoryExtractor();

    MemoryExtractor(const MemoryExtractor&) = delete;
    MemoryExtractor& operator=(const MemoryExtractor&) = delete;

    // Starts the worker thread and runs startup catch-up for any
    // conversations with unextracted messages.
    void start();

    // Fire-and-forget: schedule extraction for a conversation.
    // Thread-safe; deduplicated per conversation_id.
    void enqueue(const std::string& conversation_id);

    // Stops the worker and waits for the in-flight extraction to finish.
    // Idempotent; called by the destructor.
    void shutdown();

private:
    void worker_loop();
    void startup_catch_up();
    void process(const std::string& conversation_id);

    bool open_db();
    void close_db();
    // Watermark helpers (caller must hold db_).
    Result<long long> get_watermark(const std::string& conversation_id);
    Result<void> set_watermark(const std::string& conversation_id, long long last_message_id);

    std::shared_ptr<IProvider> provider_;
    IMemory* memory_;
    std::string db_path_;
    Config config_;

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    std::set<std::string> queued_;       // Dedup: ids currently queued or being processed
    bool stop_ = false;
    std::atomic<bool> started_{false};

    sqlite3* db_ = nullptr;              // Own connection, used only by the worker thread
};

}  // namespace ea::memory
