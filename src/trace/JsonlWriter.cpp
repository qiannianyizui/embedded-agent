#include "JsonlWriter.h"
#include "log/Logger.h"
#include <fstream>
#include <sstream>
#include <cstdio>

#ifdef __unix__
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace ea::trace {

JsonlWriter::JsonlWriter(const std::string& trace_dir,
                         StoragePolicy storage,
                         int max_entries)
    : storage_(storage)
    , max_entries_(max_entries > 0 ? max_entries : 1)
{
    fs::path dir = fs::path(trace_dir);
    std::error_code ec;
    fs::create_directories(dir, ec);
    file_path_ = (dir / "runtime-trace.jsonl").string();
}

JsonlWriter::~JsonlWriter() {
    // No persistent file handle to close — we open per write for fsync safety.
}

void JsonlWriter::write(const TraceEvent& event) {
    if (storage_ == StoragePolicy::None) return;
    json value = event.to_json();
    append_line(value);
    if (storage_ == StoragePolicy::Rolling) {
        trim_to_last_entries();
    }
}

void JsonlWriter::flush() {
    // Each write already fsyncs; nothing to flush here.
}

void JsonlWriter::append_line(const json& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Ensure parent dir exists
    if (auto parent = fs::path(file_path_).parent_path(); !parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
    }

    // Serialize to a single line
    std::string line = value.dump();

    // Open in append binary mode
    std::ofstream out(file_path_, std::ios::app | std::ios::binary);
    if (!out.is_open()) {
        EA_WARN("trace: failed to open {}", file_path_);
        return;
    }
    out.write(line.data(), static_cast<std::streamsize>(line.size()));
    out.put('\n');
    out.flush();
    out.close();

#ifdef __unix__
    // Enforce 0600 permissions (best effort)
    chmod(file_path_.c_str(), 0600);
#endif
}

void JsonlWriter::trim_to_last_entries() {
    size_t total = count_nonempty_lines(file_path_);
    if (total <= static_cast<size_t>(max_entries_)) return;

    size_t skip = total - static_cast<size_t>(max_entries_);

    // Write kept lines to a temp file, then atomic rename
    std::string tmp_path = file_path_ + ".tmp";

    std::ifstream in(file_path_);
    if (!in.is_open()) return;

    std::ofstream out(tmp_path, std::ios::trunc | std::ios::binary);
    if (!out.is_open()) {
        in.close();
        return;
    }

    size_t index = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (index >= skip) {
            out.write(line.data(), static_cast<std::streamsize>(line.size()));
            out.put('\n');
        }
        ++index;
    }
    out.flush();
    out.close();
    in.close();

#ifdef __unix__
    chmod(tmp_path.c_str(), 0600);
#endif

    std::error_code ec;
    fs::rename(tmp_path, file_path_, ec);
    if (ec) {
        EA_WARN("trace: rolling rename failed: {}", ec.message());
        fs::remove(tmp_path, ec);
    }
}

size_t JsonlWriter::count_nonempty_lines(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return 0;
    size_t n = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) ++n;
    }
    return n;
}

}  // namespace ea::trace
