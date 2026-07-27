#pragma once
#include "memory/IMemory.h"
#include "base/Result.h"
#include <string>
#include <memory>

struct sqlite3;

namespace ea::memory {

class SqliteMemory : public IMemory {
public:
    struct Config {
        std::string path;
        bool enable_fts5 = true;
        bool enable_wal = true;
    };

    explicit SqliteMemory(Config config);
    ~SqliteMemory() override;

    SqliteMemory(const SqliteMemory&) = delete;
    SqliteMemory& operator=(const SqliteMemory&) = delete;

    Result<std::string> store(const std::string& content,
                               const std::string& category,
                               int importance) override;
    Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                             int limit = 10) override;
    Result<bool> forget(const std::string& id) override;
    Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) override;
    Result<int> count() override;

    // Lifecycle hooks
    Result<void> open() override;
    Result<void> close() override;
    std::string system_prompt_block() const override;
    void on_turn_end(const std::string& assistant_output) override;
    void on_pre_compress() override;

private:
    Result<void> create_tables();

    Config config_;
    sqlite3* db_ = nullptr;
    bool opened_ = false;
};

}  // namespace ea::memory
