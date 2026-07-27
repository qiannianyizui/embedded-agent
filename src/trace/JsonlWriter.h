// JsonlWriter — JSONL append-only writer with rolling rotation.
//
// Aligned with zeroclaw's writer.rs:
//   - Append-only JSONL persistence
//   - Rolling trim: stream file → keep last N lines → atomic rename
//   - Storage policies: none / rolling / full
//   - File permissions 0600 (Unix)
#pragma once

#include "TraceEvent.h"
#include <filesystem>
#include <mutex>
#include <string>

namespace ea::trace {

enum class StoragePolicy { None, Rolling, Full };

class JsonlWriter {
public:
    JsonlWriter(const std::string& trace_dir,
                StoragePolicy storage,
                int max_entries);
    ~JsonlWriter();

    // Non-copyable
    JsonlWriter(const JsonlWriter&) = delete;
    JsonlWriter& operator=(const JsonlWriter&) = delete;

    void write(const TraceEvent& event);
    void flush();

    const std::string& file_path() const { return file_path_; }

private:
    std::string file_path_;
    StoragePolicy storage_;
    int max_entries_;
    std::mutex mutex_;

    void append_line(const json& value);
    void trim_to_last_entries();
    static size_t count_nonempty_lines(const std::string& path);
};

}  // namespace ea::trace
