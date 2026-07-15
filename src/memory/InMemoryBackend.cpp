#include "InMemoryBackend.h"
#include <sstream>

namespace ea::memory {

Result<std::string> InMemoryBackend::store(const std::string& content,
                                            const std::string& category,
                                            int importance) {
    MemoryEntry entry;
    entry.id = std::to_string(next_id_++);
    entry.content = content;
    entry.category = category;
    entry.importance = importance;
    entries_.push_back(std::move(entry));
    return std::to_string(next_id_ - 1);
}

Result<std::vector<MemoryEntry>> InMemoryBackend::recall(const std::string& query,
                                                          int limit) {
    std::vector<MemoryEntry> results;
    for (auto it = entries_.rbegin(); it != entries_.rend() && static_cast<int>(results.size()) < limit; ++it) {
        if (it->content.find(query) != std::string::npos) {
            results.push_back(*it);
        }
    }
    return results;
}

Result<bool> InMemoryBackend::forget(const std::string& id) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->id == id) {
            entries_.erase(it);
            return true;
        }
    }
    return false;
}

Result<std::vector<MemoryEntry>> InMemoryBackend::list(int limit, int offset) {
    if (offset >= static_cast<int>(entries_.size())) {
        return std::vector<MemoryEntry>{};
    }
    auto start = entries_.begin() + offset;
    auto end = start + std::min(static_cast<int>(entries_.size()) - offset, limit);
    return std::vector<MemoryEntry>(start, end);
}

Result<int> InMemoryBackend::count() {
    return static_cast<int>(entries_.size());
}

std::string InMemoryBackend::system_prompt_block() const {
    if (entries_.empty()) return {};
    std::ostringstream ss;
    ss << "# Relevant Memories\n";
    for (const auto& entry : entries_) {
        ss << "- [" << entry.category << "] " << entry.content << "\n";
    }
    return ss.str();
}

}  // namespace ea::memory
