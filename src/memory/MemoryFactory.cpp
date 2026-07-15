#include "MemoryFactory.h"
#include "SqliteMemory.h"
#include "InMemoryBackend.h"
#include "NullMemory.h"

namespace ea::memory {

std::unique_ptr<IMemory> create_backend(
    MemoryBackendType type,
    const MemoryBackendConfig& config)
{
    switch (type) {
    case MemoryBackendType::Sqlite: {
        SqliteMemory::Config cfg;
        cfg.path = config.path;
        cfg.enable_fts5 = config.enable_fts5;
        cfg.enable_wal = config.enable_wal;
        return std::make_unique<SqliteMemory>(cfg);
    }
    case MemoryBackendType::InMemory:
        return std::make_unique<InMemoryBackend>();
    case MemoryBackendType::Null:
        return std::make_unique<NullMemory>();
    }
    return std::make_unique<NullMemory>();
}

}  // namespace ea::memory
