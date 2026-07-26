#pragma once
#include "memory/IMemory.h"
#include <memory>
#include <string>

namespace ea::memory {

enum class MemoryBackendType { Sqlite, InMemory, Null };

struct MemoryBackendConfig {
    std::string path = ":memory:";
    bool enable_fts5 = true;
    bool enable_wal = false;
};

std::unique_ptr<IMemory> create_backend(
    MemoryBackendType type,
    const MemoryBackendConfig& config = {});

}  // namespace ea::memory
