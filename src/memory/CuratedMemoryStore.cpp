#include "CuratedMemoryStore.h"
#include "io/FileSystem.h"
#include "log/Logger.h"

#include <algorithm>
#include <cctype>
#include <fcntl.h>
#include <fstream>
#include <sys/file.h>
#include <unistd.h>

namespace ea::memory {

namespace {

constexpr const char* kDelimiter = "\n§\n";

std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split_entries(const std::string& text) {
    std::vector<std::string> out;
    std::string delim(kDelimiter);
    size_t start = 0;
    while (start <= text.size()) {
        size_t pos = text.find(delim, start);
        std::string piece = (pos == std::string::npos)
            ? text.substr(start)
            : text.substr(start, pos - start);
        piece = trim(piece);
        if (!piece.empty()) out.push_back(std::move(piece));
        if (pos == std::string::npos) break;
        start = pos + delim.size();
    }
    return out;
}

std::string join_entries(const std::vector<std::string>& entries) {
    std::string out;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) out += kDelimiter;
        out += entries[i];
    }
    return out;
}

class FileLock {
public:
    explicit FileLock(const std::string& path) {
        fd_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0644);
        if (fd_ >= 0) ::flock(fd_, LOCK_EX);
    }
    ~FileLock() {
        if (fd_ >= 0) {
            ::flock(fd_, LOCK_UN);
            ::close(fd_);
        }
    }
    bool valid() const { return fd_ >= 0; }

private:
    int fd_ = -1;
};

}  // namespace

CuratedMemoryStore::CuratedMemoryStore(CuratedMemoryConfig config)
    : config_(std::move(config)) {}

Result<void> CuratedMemoryStore::open() {
    if (opened_) return {};

    dir_ = config_.dir;
    if (dir_.empty()) {
        auto cfg = fs::config_dir();
        if (cfg.ok()) {
            dir_ = cfg.value() + "/memories";
        } else {
            dir_ = ".embedded-agent/memories";
        }
    }

    auto mk = fs::mkdir_p(dir_);
    if (!mk.ok()) return mk.error();

    auto mem_path = path_for(MemoryTarget::Memory);
    auto user_path = path_for(MemoryTarget::User);
    auto mem_ex = fs::exists(mem_path);
    auto user_ex = fs::exists(user_path);
    if (!mem_ex.ok() || !mem_ex.value()) {
        std::ofstream(mem_path).close();
    }
    if (!user_ex.ok() || !user_ex.value()) {
        std::ofstream(user_path).close();
    }

    opened_ = true;
    return reload();
}

Result<void> CuratedMemoryStore::reload() {
    if (!opened_) {
        auto r = open();
        if (!r.ok()) return r.error();
    }
    auto mem = read_file(path_for(MemoryTarget::Memory));
    if (!mem.ok()) return mem.error();
    auto user = read_file(path_for(MemoryTarget::User));
    if (!user.ok()) return user.error();
    memory_entries_ = std::move(mem.value());
    user_entries_ = std::move(user.value());
    return {};
}

std::string CuratedMemoryStore::path_for(MemoryTarget target) const {
    return dir_ + "/" + (target == MemoryTarget::User ? "USER.md" : "MEMORY.md");
}

std::vector<std::string>& CuratedMemoryStore::entries_for(MemoryTarget target) {
    return target == MemoryTarget::User ? user_entries_ : memory_entries_;
}

const std::vector<std::string>& CuratedMemoryStore::entries_for(
        MemoryTarget target) const {
    return target == MemoryTarget::User ? user_entries_ : memory_entries_;
}

int CuratedMemoryStore::char_limit_for(MemoryTarget target) const {
    return target == MemoryTarget::User ? config_.user_char_limit
                                        : config_.memory_char_limit;
}

Result<std::vector<std::string>> CuratedMemoryStore::entries(
        MemoryTarget target) const {
    if (!opened_) return Error::db("CuratedMemoryStore is not open");
    return entries_for(target);
}

Result<void> CuratedMemoryStore::add(MemoryTarget target,
                                     const std::string& content) {
    if (!opened_) {
        auto r = open();
        if (!r.ok()) return r.error();
    }
    std::string entry = trim(content);
    if (entry.empty()) return Error::invalid_arg("memory entry is empty");

    auto& list = entries_for(target);
    for (const auto& e : list) {
        if (e == entry) return {};  // idempotent
    }

    int limit = char_limit_for(target);
    int total = static_cast<int>(entry.size());
    for (const auto& e : list) total += static_cast<int>(e.size());
    if (!list.empty()) total += 2;  // delimiter overhead
    if (total > limit) {
        return Error::invalid_arg(
            "memory char limit exceeded (" + std::to_string(limit) + ")");
    }

    std::string path = path_for(target);
    FileLock lock(path + ".lock");
    if (!lock.valid()) return Error::io("cannot lock " + path);

    auto current = read_file(path);
    if (!current.ok()) return current.error();
    list = std::move(current.value());
    for (const auto& e : list) {
        if (e == entry) return {};
    }
    list.push_back(entry);
    return write_file(path, list);
}

Result<bool> CuratedMemoryStore::remove(MemoryTarget target,
                                        const std::string& substring) {
    if (!opened_) {
        auto r = open();
        if (!r.ok()) return r.error();
    }
    if (substring.empty()) return Error::invalid_arg("substring is empty");

    std::string path = path_for(target);
    FileLock lock(path + ".lock");
    if (!lock.valid()) return Error::io("cannot lock " + path);

    auto current = read_file(path);
    if (!current.ok()) return current.error();
    auto& list = entries_for(target);
    list = std::move(current.value());

    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->find(substring) != std::string::npos) {
            list.erase(it);
            auto w = write_file(path, list);
            if (!w.ok()) return w.error();
            return true;
        }
    }
    return false;
}

bool CuratedMemoryStore::empty(MemoryTarget target) const {
    return entries_for(target).empty();
}

std::string CuratedMemoryStore::format_for_system_prompt(
        MemoryTarget target) const {
    const auto& list = entries_for(target);
    if (list.empty()) return {};
    std::string out;
    for (const auto& e : list) {
        if (!out.empty()) out += "\n";
        out += "- " + e;
    }
    return out;
}

Result<std::vector<std::string>> CuratedMemoryStore::read_file(
        const std::string& path) const {
    std::ifstream in(path);
    if (!in.is_open()) return std::vector<std::string>{};
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    return split_entries(text);
}

Result<void> CuratedMemoryStore::write_file(
        const std::string& path,
        const std::vector<std::string>& entries) const {
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp);
        if (!out.is_open()) return Error::io("cannot write " + tmp);
        out << join_entries(entries);
        if (!entries.empty()) out << "\n";
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        return Error::io("cannot rename " + tmp + " -> " + path);
    }
    return {};
}

}  // namespace ea::memory
