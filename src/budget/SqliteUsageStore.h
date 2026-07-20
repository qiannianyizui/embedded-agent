// SqliteUsageStore — SQLite-backed usage record persistence
#pragma once
#include "IUsageStore.h"
#include <string>
#include <mutex>
#include <random>

struct sqlite3;

namespace ea::budget {

class SqliteUsageStore : public IUsageStore {
public:
    struct Config {
        std::string path;
        bool enable_wal = true;
    };

    explicit SqliteUsageStore(Config config);
    ~SqliteUsageStore() override;

    SqliteUsageStore(const SqliteUsageStore&) = delete;
    SqliteUsageStore& operator=(const SqliteUsageStore&) = delete;

    Result<std::string> record(const UsageRecord& rec) override;
    Result<std::vector<UsageRecord>> query(
        const std::string& session_id = "",
        int limit = 100, int offset = 0) override;
    Result<UsageSnapshot> total_usage(
        const std::string& session_id = "") override;
    Result<CostSnapshot> total_cost(
        const std::string& session_id = "") override;
    Result<bool> clear(const std::string& session_id = "") override;
    Result<void> open() override;
    Result<void> close() override;

private:
    Result<void> create_tables();
    static std::string generate_id();
    static std::string current_iso8601();

    Config config_;
    sqlite3* db_ = nullptr;
    bool opened_ = false;
    static std::mutex id_mutex_;
    static std::mt19937 rng_;
};

}  // namespace ea::budget
