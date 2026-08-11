#pragma once
#include "base/Result.h"
#include <string>
#include <vector>

namespace ea::memory {

enum class MemoryTarget {
    Memory,  // MEMORY.md — agent notes
    User,    // USER.md — user profile
};

struct CuratedMemoryConfig {
    std::string dir;                 // Empty = ~/.embedded-agent/memories
    int memory_char_limit = 2200;
    int user_char_limit = 1375;
};

// File-backed curated memory (Hermes-style MEMORY.md / USER.md).
// Entries are separated by "\n§\n"; writes are atomic + file-locked.
class CuratedMemoryStore {
public:
    explicit CuratedMemoryStore(CuratedMemoryConfig config = {});

    Result<void> open();   // Ensure directory/files exist and load entries
    Result<void> reload();

    Result<std::vector<std::string>> entries(MemoryTarget target) const;
    Result<void> add(MemoryTarget target, const std::string& content);
    Result<bool> remove(MemoryTarget target, const std::string& substring);
    bool empty(MemoryTarget target) const;

    // Rendered block body (without the "# User Profile" heading).
    std::string format_for_system_prompt(MemoryTarget target) const;

    const std::string& dir() const { return dir_; }

private:
    std::string path_for(MemoryTarget target) const;
    std::vector<std::string>& entries_for(MemoryTarget target);
    const std::vector<std::string>& entries_for(MemoryTarget target) const;
    int char_limit_for(MemoryTarget target) const;

    Result<std::vector<std::string>> read_file(const std::string& path) const;
    Result<void> write_file(const std::string& path,
                            const std::vector<std::string>& entries) const;

    CuratedMemoryConfig config_;
    std::string dir_;
    std::vector<std::string> memory_entries_;
    std::vector<std::string> user_entries_;
    bool opened_ = false;
};

}  // namespace ea::memory
